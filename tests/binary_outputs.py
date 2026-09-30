"""Native BSP and SaveFile failure cleanup. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

from fixtures import create_fixture
from integration import run


@contextmanager
def output_failure(destination, maximum=128):
    """Deny publication on Windows; enforce a real write limit on POSIX."""
    if os.name == 'nt':
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                      wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel.CloseHandle.restype = wintypes.BOOL
        handle = kernel.CreateFileW(str(destination), 0x80000000, 1 | 2, None, 3, 0, None)
        assert handle not in (None, ctypes.c_void_p(-1).value), ctypes.get_last_error()
        try:
            yield {}
        finally:
            assert kernel.CloseHandle(handle)
    else:
        import resource
        import signal
        def bounded_output():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            _, hard = resource.getrlimit(resource.RLIMIT_FSIZE)
            resource.setrlimit(resource.RLIMIT_FSIZE, (maximum, hard))
        yield {'preexec_fn': bounded_output}


def without_timestamp(data):
    # Ignore only the unused writer timestamp, preserving its size and every
    # directory entry, payload, padding byte and ordering decision.
    data, count = re.subn(rb'(I LOVE MY Q3MAP2 [^\n]+ on )([^\n]+)\n\x00',
                         lambda m: m[1] + b'\0' * len(m[2]) + b'\n\0', data)
    assert count == 1, count
    return data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--reference', type=Path)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    checked = []
    skips = []
    parity = []

    def failed(base, arguments, label, expected, **options):
        result = subprocess.run([str(exe), *map(str, [*base, *arguments])], cwd=root,
                                capture_output=True, timeout=60, **options)
        log = result.stdout + result.stderr
        (root / (label + '.log')).write_bytes(log)
        assert result.returncode == 1 and b'ERROR' in log, (label, result.returncode, log[-4000:])
        messages = (expected,) if isinstance(expected, bytes) else expected
        assert any(message in log for message in messages), (label, log[-4000:])
        assert b'Wrote ' not in log and b'CPU profile:' not in log, (label, 'Premature success message')
        assert not list(root.rglob('*.q3mapx-*.tmp')), (label, 'Failed write left a staging file')
        checked.append(label)
        return log

    def reject_link(path, source, arguments, base, label):
        try:
            if not path.is_symlink():
                path.symlink_to(source)
        except OSError as error:
            if os.name != 'nt' or error.winerror not in (5, 1314):
                raise
            skips.append(label + ': symbolic links require Windows privileges')
        else:
            before = source.read_bytes()
            failed(base, arguments, label, b'Output must be a regular file')
            assert path.is_symlink() and source.read_bytes() == before

    for game, magic in (('quake3', b'IBSP'), ('ja', b'RBSP')):
        directory = root / game
        source = create_fixture(directory)
        base = ['-game', game, '-fs_basepath', directory, '-fs_basegame', 'baseq3', '-threads', 2]
        run(exe, [*base, '-meta', source], directory, 'compile')
        original = source.with_suffix('.bsp').read_bytes()
        assert original[:4] == magic
        source_bytes = source.read_bytes()

        # Exercise buffered header seek failures and an immediate large-lump
        # fwrite failure separately, using the same two native serializers.
        for kind in ('empty-world', 'small', 'large'):
            path = directory / (kind + '.bsp')
            data = bytearray(original)
            if kind == 'empty-world':
                # Valid empty world: fit wholly inside any normal stdio buffer
                # so the actual OS error is encountered when seeking the header.
                data = bytearray(original[:8]) + bytearray(18 * 8)
                for lump, payload in ((0, b'{\n"classname" "worldspawn"\n}\n\0'), (7, bytes(40))):
                    struct.pack_into('<ii', data, 8 + lump * 8, len(data), len(payload))
                    data.extend(payload)
                    data.extend(b'\0' * (-len(data) % 4))
            if kind == 'large':
                struct.pack_into('<ii', data, 8 + 14 * 8, len(data), 128 * 128 * 3 * 8)
                data.extend(bytes(range(256)) * (128 * 128 * 3 * 8 // 256))
            path.write_bytes(data)
            output = path.with_name(path.stem + '_s.bsp')
            output.write_bytes(b'previous BSP output')
            with output_failure(output) as options:
                expected = {'empty-world': b'Cannot seek output', 'large': b'Cannot write output',
                            'small': (b'Cannot write output', b'Cannot seek output')}[kind]
                if os.name == 'nt':
                    expected = b'Cannot replace'
                failed(base, ['-scale', 1, path], game + '-' + kind, expected, **options)
            assert output.read_bytes() == b'previous BSP output' and path.read_bytes() == data
            run(exe, [*base, '-scale', 1, path], directory, 'retry-' + kind)
            saved = output.read_bytes()
            assert saved[:4] == magic and path.read_bytes() == data
            run(exe, [*base, '-info', output], directory, 'read-back-' + kind)
            record = {'game': game, 'fixture': kind, 'bytes': len(saved),
                      'normalized_sha256': hashlib.sha256(without_timestamp(saved)).hexdigest()}
            if args.reference:
                run(args.reference.resolve(), [*base, '-scale', 1, path], directory, 'reference-' + kind)
                assert without_timestamp(output.read_bytes()) == without_timestamp(saved), (game, kind, 'Changed BSP bytes')
                record['reference_byte_parity'] = True
            parity.append(record)

        blocked = directory / 'blocked.bsp'
        blocked.write_bytes(original)
        destination = directory / 'blocked_s.bsp'
        destination.mkdir(exist_ok=True)
        (destination / 'preserved').write_bytes(b'keep directory')
        failed(base, ['-scale', 1, blocked], game + '-directory', b'Output must be a regular file')
        assert (destination / 'preserved').read_bytes() == b'keep directory'
        linked_input = directory / 'linked.bsp'
        linked_input.write_bytes(original)
        reject_link(directory / 'linked_s.bsp', linked_input, ['-scale', 1, linked_input], base, game + '-link')
        assert source.read_bytes() == source_bytes and source.with_suffix('.bsp').read_bytes() == original

    # -info is read-only; its profile exercises shared SaveFile independently of
    # BSP publication. A small buffered write fails at fclose, before rename.
    bsp = source.with_suffix('.bsp')
    profile = root / 'profile.json'
    profile.write_bytes(b'previous profile')
    with output_failure(profile, 1) as options:
        failed(base, ['-profile', profile, '-info', bsp], 'profile-buffered-write',
               b'Cannot replace' if os.name == 'nt' else b'Cannot finish output', **options)
    assert profile.read_bytes() == b'previous profile'
    run(exe, [*base, '-profile', profile, '-info', bsp], root, 'profile-retry')
    metadata = json.loads(profile.read_text())
    assert metadata['exit_code'] == 0 and metadata['requested_workers'] == 2
    blocked_profile = root / 'blocked-profile.json'
    blocked_profile.mkdir(exist_ok=True)
    (blocked_profile / 'preserved').write_bytes(b'keep profile directory')
    failed(base, ['-profile', blocked_profile, '-info', bsp], 'profile-directory', b'Output must be a regular file')
    assert (blocked_profile / 'preserved').read_bytes() == b'keep profile directory'
    linked_profile = root / 'linked-profile.json'
    reject_link(linked_profile, bsp, ['-profile', linked_profile, '-info', bsp], base, 'profile-link')
    assert bsp.read_bytes() == original and not list(root.rglob('*.q3mapx-*.tmp'))
    report = {'schema_version': 1, 'platform': os.name, 'compiler_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
              'checks': checked, 'skips': skips, 'successful_outputs': parity, 'result': 'passed'}
    if args.reference:
        report['reference_sha256'] = hashlib.sha256(args.reference.read_bytes()).hexdigest()
    (root / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'{len(checked)} binary output failure checks, six successful rewrites and profile retry passed')
    for skip in skips:
        print('SKIP:', skip)


if __name__ == '__main__':
    main()

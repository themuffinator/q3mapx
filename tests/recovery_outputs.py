"""Preserve MAP/report outputs on actual filesystem failures. GPL-3.0-or-later."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from fixtures import create_fixture
from integration import run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(strict=True), args.work_dir.resolve()
    source = create_fixture(root)
    base = ['-game', 'quake3', '-fs_basepath', root, '-threads', 4]
    run(exe, [*base, '-meta', source], root, 'compile')
    bsp = source.with_suffix('.bsp')
    original = bsp.read_bytes()
    checked = []

    def recover(output, label, *, report=None, success=False, legacy=False, format='map_220', **options):
        arguments = [*base, *(['-convert'] if legacy else ['-decompile']), '-format', format, '-o', output]
        if report is not None:
            arguments += ['-report', report]
        result = subprocess.run([str(exe), *map(str, arguments), str(bsp)], cwd=root,
                                capture_output=True, timeout=60, **options)
        log = result.stdout + result.stderr
        (root / (label + '.log')).write_bytes(log)
        assert result.returncode == (0 if success else 1), (label, result.returncode, log[-4000:])
        if not success:
            assert b'ERROR' in log, (label, log[-4000:])
        assert bsp.read_bytes() == original, 'Recovery modified the BSP source'
        assert not list(root.rglob('*.q3mapx-*.tmp')), 'Uncommitted output or rollback files remained'
        checked.append(label)
        return log

    # The old writer replaces the MAP before it discovers a report-path error.
    output = root / 'blocked-report.map'
    output.write_bytes(b'previous editable map')
    report = root / 'blocked-report.json'
    report.mkdir(exist_ok=True)
    (report / 'preserved').write_bytes(b'previous directory')
    recover(output, 'report-directory', report=report)
    assert output.read_bytes() == b'previous editable map'
    assert (report / 'preserved').read_bytes() == b'previous directory'

    output = root / 'blocked-map.map'
    output.mkdir(exist_ok=True)
    (output / 'preserved').write_bytes(b'previous map directory')
    report = root / 'preserved-report.json'
    report.write_bytes(b'previous recovery report')
    recover(output, 'map-directory', report=report)
    assert report.read_bytes() == b'previous recovery report'
    assert (output / 'preserved').read_bytes() == b'previous map directory'

    # All three map encodings, default report naming and the legacy no-report path.
    for format in ('map', 'map_bp', 'map_220'):
        output = root / ('success-' + format + '.map')
        report = Path(str(output) + '.recovery.json')
        output.write_bytes(b'previous map'); report.write_bytes(b'previous report')
        recover(output, 'replace-' + format, success=True, format=format)
        metadata = json.loads(report.read_text(encoding='utf-8'))
        assert metadata['format'] == format and metadata['brushes'] == 8
        assert b'patchDef2' in output.read_bytes()
    output = root / 'legacy.map'
    recover(output, 'legacy-without-report', success=True, legacy=True)
    assert not Path(str(output) + '.recovery.json').exists()
    recover(output, 'legacy-explicit-report', success=True, legacy=True, report=root / 'legacy.json')
    assert json.loads((root / 'legacy.json').read_text(encoding='utf-8'))['output'] == str(output)

    if os.name == 'nt':
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                       wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]; kernel.CloseHandle.restype = wintypes.BOOL
        for existing in (False, True):
            output = root / ('locked-' + str(existing) + '.map')
            report = Path(str(output) + '.recovery.json')
            output.write_bytes(b'previous locked map')
            if existing:
                report.write_bytes(b'previous report')
            elif report.is_file():
                report.unlink()
            handle = kernel.CreateFileW(str(output), 0x80000000, 1 | 2, None, 3, 0, None)
            assert handle not in (None, ctypes.c_void_p(-1).value), ctypes.get_last_error()
            try:
                log = recover(output, 'rollback-report-' + str(existing))
                assert b'Cannot replace' in log, log[-4000:]
            finally:
                assert kernel.CloseHandle(handle)
            assert output.read_bytes() == b'previous locked map'
            assert report.read_bytes() == b'previous report' if existing else not report.exists()
            recover(output, 'retry-unlocked-' + str(existing), success=True)
    else:
        import resource
        import signal
        def limit(size):
            def apply():
                signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
                _, hard = resource.getrlimit(resource.RLIMIT_FSIZE)
                resource.setrlimit(resource.RLIMIT_FSIZE, (size, hard))
            return apply
        for legacy, maximum in ((True, 4096), (False, 512)):
            output = root / ('limited-' + str(legacy) + '.map')
            report = Path(str(output) + '.recovery.json')
            output.write_bytes(b'previous map'); report.write_bytes(b'previous report')
            log = recover(output, 'write-limit-' + str(legacy), legacy=legacy, preexec_fn=limit(maximum))
            assert b'Cannot finish output' in log or b'Cannot write recovery report' in log, log[-4000:]
            if not legacy:
                assert b'recovery.json' in log, 'Report write failure was not diagnosed'
            assert output.read_bytes() == b'previous map' and report.read_bytes() == b'previous report'

    linked = root / 'linked.map'
    try:
        if not linked.is_symlink():
            linked.symlink_to(bsp)
    except OSError as error:
        if os.name != 'nt' or error.winerror not in (5, 1314):
            raise
        print('SKIP: creating a symbolic link requires Windows privileges')
    else:
        recover(linked, 'reject-linked-map')
        assert linked.is_symlink() and bsp.read_bytes() == original
        assert not Path(str(linked) + '.recovery.json').exists()
    evidence = {'schema_version': 1, 'compiler_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
                'platform': os.name, 'checks': checked, 'result': 'passed'}
    (root / 'validation.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print('MAP/report publication, rollback, buffered failures, formats and legacy recovery passed')


if __name__ == '__main__':
    main()

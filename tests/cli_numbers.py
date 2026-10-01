"""Strict numeric CLI syntax, native output parity and preserved-output failures.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from fixtures import create_fixture
from integration import Bsp, run


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--reference', type=Path, help='Preceding compiler, for valid-input parity only')
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    source = create_fixture(root)
    bsp = source.with_suffix('.bsp')
    base = ['-game', 'quake3', '-fs_basepath', root, '-fs_homepath', root / 'home', '-threads', '1']
    for name, options in [('bsp', ['-meta']), ('vis', ['-vis', '-saveprt']), ('light', ['-light', '-fast'])]:
        run(exe, [*base, *options, source], root, 'setup-' + name)
    inputs = {path: path.read_bytes() for path in
              [source, bsp, source.with_suffix('.prt'), source.with_suffix('.srf')]}

    def restore_inputs():
        for path, data in inputs.items():
            path.write_bytes(data)

    recovered = root / 'recovered.map'
    minimap = root / 'minimap.tga'
    report = root / 'report.json'
    # These are equivalent numbers through real successful stages, not parser stubs.
    valid = [
        ('bsp', ['-meta', '-subdivisions', '8', '-metaadequatescore', '-1', '-ne', '.00001', '-de', '.01'],
         ['-meta', '-subdivisions', '+008', '-metaadequatescore', '-01', '-ne', '+1e-5', '-de', '+.01'], source, bsp),
        ('integer-minimum', ['-meta', '-metaadequatescore', '-2147483648'],
         ['-meta', '-metaadequatescore', '-02147483648'], source, bsp),
        ('integer-maximum', ['-meta', '-metaadequatescore', '2147483647'],
         ['-meta', '-metaadequatescore', '+2147483647'], source, bsp),
        ('light', ['-light', '-fast', '-bounce', '0', '-contrast', '-12.5', '-gamma', '1', '-brightness', '.5', '-samples', '1'],
         ['-light', '-fast', '-bounce', '-0', '-contrast', '-1.25e+1', '-gamma', '+1.', '-brightness', '+.5', '-samples', '+01'], bsp, bsp),
        ('minimap', ['-minimap', '-backend', 'cpu', '-o', minimap, '-size', '32', '-samples', '1', '-seed', '0', '-brightness', '-.025', '-contrast', '1.25', '-border', '.125', '-sharpen', '-1'],
         ['-minimap', '-backend', 'cpu', '-o', minimap, '-size', '+0032', '-samples', '+001', '-seed', '-0', '-brightness', '-2.5e-2', '-contrast', '+1.25', '-border', '+.125', '-sharpen', '-1.0'], bsp, minimap),
        ('decompile', ['-decompile', '-o', recovered, '-ne', '.00001', '-de', '.01'],
         ['-decompile', '-o', recovered, '-ne', '+1e-5', '-de', '+.01'], bsp, recovered),
    ]
    valid_results = []
    for name, canonical, decorated, input_path, output in valid:
        outcomes, raw_outcomes, command_lines = [], [], []
        variants = [('canonical', exe, canonical), ('decorated', exe, decorated)]
        if args.reference:
            variants.append(('reference', args.reference.resolve(), decorated))
        for variant, compiler, options in variants:
            restore_inputs()
            run(compiler, [*base, '-threads', '+001', *options, input_path], root, name + '-' + variant)
            if output == bsp:
                native = Bsp(output)
                raw = [sha(native.lump(i)) for i in range(17)]
                # Different argument spellings intentionally change this provenance
                # key. Check every other entity byte, and retain full raw hashes for
                # exact preceding-compiler comparison with identical arguments.
                command = re.findall(br'^"_q3map2_cmdline" "([^"\r\n]*)"\r?$', native.lump(0), re.M)
                assert len(command) == 1, command
                entities, count = re.subn(br'^"_q3map2_cmdline" "[^"\r\n]*"\r?\n', b'', native.lump(0), flags=re.M)
                assert count == 1
                command_lines.append(command[0].decode())
                outcome = [sha(entities), *raw[1:]]
            else:
                raw = outcome = sha(output.read_bytes())
            outcomes.append(outcome); raw_outcomes.append(raw)
            (root / (name + '-' + variant + output.suffix)).write_bytes(output.read_bytes())
        assert all(value == outcomes[0] for value in outcomes), (name, outcomes)
        if args.reference:
            assert raw_outcomes[1] == raw_outcomes[2], (name, 'preceding compiler changed raw output')
        valid_results.append({'case': name, 'canonical': list(map(str, canonical)),
                              'decorated': list(map(str, decorated)), 'matching_outputs': len(outcomes),
                              'recorded_command_lines': command_lines, 'raw_outputs': raw_outcomes,
                              'reference_exact_raw_parity': True if args.reference else None,
                              'bsp_lump_sha256' if output == bsp else 'output_sha256': outcomes[0]})

    restore_inputs()
    sentinels = [recovered, Path(str(recovered) + '.recovery.json'), minimap, report,
                 root / 'profile.json', root / 'compute.json', source.with_suffix('.lin'), source.with_suffix('.reg')]
    for path in sentinels:
        path.write_bytes(('Previous output: ' + path.name + '\n').encode())
    protected = {**inputs, **{path: path.read_bytes() for path in sentinels}}
    common = ['', '+', '-', '+-0', '+-1', '+-.5', '++1', '--1', '-+1', '+--1',
              ' +1', '1 ', '1junk', '0x1', 'nan', 'inf', '1e999', '1e-999', '1e+-1', '1e-+1']
    cases = [
        ('global-workers', [], '-threads', source, True),
        ('global-patches', [], '-subdivisions', source, True),
        ('bsp-score', ['-meta'], '-metaadequatescore', source, True),
        ('light-bounces', ['-light', '-fast'], '-bounce', bsp, True),
        ('light-contrast', ['-light', '-fast'], '-contrast', bsp, False),
        ('minimap-seed', ['-minimap', '-backend', 'cpu', '-size', '32', '-o', minimap, '-compute-report', root / 'compute.json'], '-seed', bsp, True),
        ('minimap-brightness', ['-minimap', '-backend', 'cpu', '-size', '32', '-o', minimap], '-brightness', bsp, False),
        ('decompile-epsilon', ['-decompile', '-o', recovered], '-ne', bsp, False),
        ('evidence-depth', ['-bsp-evidence', '-report', report], '-region-depth', bsp, True),
    ]
    rejected = []
    for name, options, option, input_path, integer in cases:
        spellings = common + (['1.0', '1e0', '2147483648', '-2147483649'] if integer else [])
        for index, spelling in enumerate(spellings):
            command = [*base, '-profile', root / 'profile.json', *options, option, spelling, input_path]
            result = subprocess.run([str(exe), *map(str, command)], cwd=root,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
            text = result.stdout.decode('utf-8', errors='replace')
            (root / f'rejected-{name}-{index:02}.log').write_text(text, encoding='utf-8')
            assert result.returncode == 1 and option + ' expects ' in text, (name, spelling, result.returncode, text[-2500:])
            assert not any(value in text for value in ['AddressSanitizer', 'runtime error:', 'std::bad_alloc'])
            assert all(path.read_bytes() == data for path, data in protected.items()), (name, spelling, 'modified output')
            rejected.append({'case': name, 'option': option, 'value': spelling, 'exit_code': 1,
                             'diagnostic': 'integer' if integer else 'finite number', 'inputs_and_outputs_preserved': True})
    record = {'schema_version': 1, 'valid_cases': valid_results, 'rejected_cases': rejected,
              'protected_files': [path.relative_to(root).as_posix() for path in protected]}
    (root / 'results.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(f'{len(valid_results)} equivalent native output pairs and {len(rejected)} preserved-output numeric failures passed', flush=True)


if __name__ == '__main__':
    main()

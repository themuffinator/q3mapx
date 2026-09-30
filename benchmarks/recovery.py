"""Compare whole-command MAP recovery with exact output checks. GPL-3.0-or-later."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from fixtures import create_fixture
from integration import Bsp, run


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--map', type=Path, help='Existing BSP; recover a private copy without changing the original')
    parser.add_argument('--game', default='quake3')
    parser.add_argument('--game-root', type=Path, help='Read-only installed asset root for an existing BSP')
    parser.add_argument('--grid', type=int, default=47)
    parser.add_argument('--threads', type=int, nargs='+', default=[1, 4, 20])
    parser.add_argument('--repeat', type=int, default=5)
    parser.add_argument('--format', choices=['map', 'map_bp', 'map_220'], default='map_220')
    parser.add_argument('--fast', action='store_true')
    args = parser.parse_args()
    if args.repeat < 3 or len(set(args.threads)) != len(args.threads) or any(t < 1 or t > 1024 for t in args.threads):
        parser.error('Use at least three measured runs and distinct worker counts in 1..1024')
    if not args.map and (args.game != 'quake3' or args.game_root or args.grid < 3 or args.grid > 63 or args.grid % 2 == 0):
        parser.error('Generated maps require quake3, local generated assets and an odd grid of 3..63')
    root = args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    methods = [('baseline', args.baseline.resolve(strict=True)), ('q3mapx', args.compiler.resolve(strict=True))]
    hashes = {name: digest(exe.read_bytes()) for name, exe in methods}
    if args.map:
        source = args.map.resolve(strict=True)
        original = source.read_bytes()
        assets = args.game_root.resolve(strict=True) if args.game_root else root
        counts = None
    else:
        assets = root / 'assets'
        source_map = create_fixture(assets, dense=True, grid=args.grid)
        run(methods[0][1], ['-game', args.game, '-fs_basepath', assets, '-threads', 1, '-meta', source_map], root, 'prepare', timeout=300)
        source = source_map.with_suffix('.bsp')
        original = source.read_bytes()
        counts = Bsp(source).summary()
    bsp, output = root / 'input.bsp', root / 'recovered.map'
    if source == bsp or source == output:
        parser.error('Use a separate work directory from the source BSP')
    bsp.write_bytes(original)
    expected_map = expected_report = None
    records, summaries = [], []
    for workers in args.threads:
        arguments = ['-game', args.game, '-fs_basepath', assets, '-threads', workers,
                     '-decompile', '-format', args.format, *(['-fast'] if args.fast else []), '-o', output, bsp]
        for iteration in range(args.repeat + 1):
            for name, exe in methods[iteration % 2:] + methods[:iteration % 2]:
                result = run(exe, arguments, root, f'{name}-{workers}-{iteration}', timeout=300)
                recovered = output.read_bytes()
                report = json.loads(Path(str(output) + '.recovery.json').read_text(encoding='utf-8'))
                if expected_map is None:
                    expected_map, expected_report = recovered, report
                assert recovered == expected_map, f'{name}/{workers}: MAP bytes changed'
                assert report == expected_report, f'{name}/{workers}: recovery report changed'
                records.append({'implementation': name, 'workers': workers, 'warmup': iteration == 0, **result})
                print(name, workers, iteration, f"{result['seconds']:.4f}s", 'MAP/report identical', flush=True)
        for name, _ in methods:
            samples = [r['seconds'] for r in records if r['implementation'] == name and r['workers'] == workers and not r['warmup']]
            summaries.append({'implementation': name, 'workers': workers, 'seconds': samples,
                              'median_seconds': statistics.median(samples)})
    assert source.read_bytes() == original and bsp.read_bytes() == original, 'Source BSP changed'
    for name, exe in methods:
        assert digest(exe.read_bytes()) == hashes[name], 'Executable changed during benchmark'
    report = {'schema_version': 1, 'kind': 'whole_process_map_recovery', 'game': args.game,
              'fixture_kind': 'local_native_map' if args.map else f'dense-room-{args.grid}x{args.grid}',
              'timestamp_utc': datetime.now(timezone.utc).isoformat(), 'platform': platform.platform(),
              'processor': platform.processor(), 'logical_cpus': os.cpu_count(), 'format': args.format,
              'fast': args.fast, 'input_sha256': digest(original), 'input_bytes': len(original), 'counts': counts,
              'compiler_sha256': hashes, 'warmup_runs': 1, 'measured_runs': args.repeat,
              'map_bytes': len(expected_map), 'map_sha256': digest(expected_map),
              'recovery_counts': {key: value for key, value in expected_report.items() if isinstance(value, (int, bool))},
              'recovery_report_sha256': digest(json.dumps(expected_report, sort_keys=True).encode('utf-8')),
              'exact_map_and_report_parity': True,
              'summaries': summaries, 'records': records}
    (root / 'benchmark.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(summaries, indent=2))


if __name__ == '__main__':
    main()

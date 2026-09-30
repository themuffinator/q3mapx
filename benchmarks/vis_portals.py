"""Matched VIS merge/bitset measurements. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
from integration import Bsp, run
from vis_fixtures import create_vis_fixture
from vis_merge import rows


def distribution(values):
    values = sorted(values)
    return {'min': min(values), 'median': statistics.median(values),
            'p95': values[min(len(values)-1, int(.95*len(values)))], 'max': max(values),
            'mean': statistics.mean(values)}


def visibility_cost(bsp):
    matrix = rows(bsp)
    leaves, refs, surfaces = bsp.lump(4), bsp.lump(5), bsp.lump(13)
    cluster_surfaces = [set() for _ in matrix]
    for at in range(0, len(leaves), 48):
        cluster = struct.unpack_from('<i', leaves, at)[0]
        if cluster < 0:
            continue
        start, count = struct.unpack_from('<ii', leaves, at+32)
        cluster_surfaces[cluster].update(struct.unpack_from(f'<{count}i', refs, start*4))
    triangles = [struct.unpack_from('<i', surfaces, at+24)[0]//3 for at in range(0, len(surfaces), 104)]
    visible_surfaces, visible_triangles = [], []
    for row in matrix:
        visible = set()
        while row:
            bit = row & -row
            visible.update(cluster_surfaces[bit.bit_length()-1])
            row ^= bit
        visible_surfaces.append(len(visible))
        visible_triangles.append(sum(triangles[index] for index in visible))
    return {'clusters': len(matrix), 'vis_bytes': len(bsp.lump(16)),
            'visible_cluster_pairs': sum(row.bit_count() for row in matrix),
            'visible_world_surfaces': distribution(visible_surfaces),
            'visible_world_triangles': distribution(visible_triangles)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--reference', type=Path, required=True,
                        help='Same merge fixes, before bitset compaction')
    parser.add_argument('--legacy', type=Path, help='Pre-round compiler; output differences are recorded')
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--grid', type=int, default=9)
    parser.add_argument('--threads', type=int, nargs='+', default=[1, 20])
    parser.add_argument('--repeat', type=int, default=5)
    args = parser.parse_args()
    if args.repeat < 2:
        parser.error('At least two measured runs required')
    methods = [('uncompressed', args.reference.resolve()), ('compact', args.compiler.resolve())]
    if args.legacy:
        methods.append(('legacy', args.legacy.resolve()))
    hashes = {name: hashlib.sha256(exe.read_bytes()).hexdigest() for name, exe in methods}
    root = args.work_dir.resolve()
    records, fixtures = [], []
    for detail in (False, True):
        fixture = 'manual-detail' if detail else 'poor-detail'
        directory = root / fixture
        source = create_vis_fixture(directory, grid=args.grid, detail=detail)
        base = ['-game', 'quake3', '-fs_basepath', directory]
        build = run(args.compiler.resolve(), [*base, '-threads', 1, '-meta', source], directory, 'bsp')
        target = source.with_suffix('.bsp')
        original = target.read_bytes()
        fixtures.append({'name': fixture, 'map_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                         'bsp_sha256': hashlib.sha256(original).hexdigest(),
                         'prt_sha256': hashlib.sha256(source.with_suffix('.prt').read_bytes()).hexdigest(),
                         'bsp_seconds_single_observation': build['seconds'], 'counts': Bsp(target).summary()})
        baseline = None
        expected_modes = {}
        for mode, options in (('default', []), ('merge', ['-merge']),
                              ('mergeportals', ['-mergeportals']), ('hint', ['-hint'])):
            for workers in args.threads:
                timings = {name: [] for name, _ in methods}
                profiles = {name: [] for name, _ in methods}
                observed = {}
                for iteration in range(args.repeat+1):
                    order = methods[iteration % len(methods):] + methods[:iteration % len(methods)]
                    for name, exe in order:
                        target.write_bytes(original)
                        label = f'{mode}-{workers}-{iteration}-{name}'
                        profile = directory / (label+'.json')
                        result = run(exe, [*base, '-threads', workers, '-profile', profile,
                                          '-vis', '-reproducible', '-saveprt', *options, source], directory, label, timeout=600)
                        bsp = Bsp(target)
                        output, matrix = bsp.lump(16), rows(bsp)
                        if name != 'legacy':
                            if mode not in expected_modes:
                                expected_modes[mode] = output
                            assert output == expected_modes[mode], (fixture, mode, name, workers, iteration)
                            if mode == 'default':
                                baseline = matrix
                        log = (directory/(label+'.log')).read_text()
                        match = re.search(r'VIS portal bitsets: (\d+) live / (\d+) input directions; (\d+) -> (\d+) bytes each', log)
                        bitsets = dict(zip(('live_directions', 'input_directions', 'original_bytes', 'bytes'), map(int, match.groups()))) if match else None
                        memory = re.search(r'(\d+) bytes required passage memory \((\d+) passages\)', log)
                        observations = {'visibility_sha256': hashlib.sha256(output).hexdigest(),
                                        'active_directions': int(re.search(r'(\d+) active portals', log)[1]),
                                        'visibility_cost': visibility_cost(bsp), 'bitsets': bitsets,
                                        'passage_allocation_bytes': int(memory[1]) if memory else None,
                                        'passages': int(memory[2]) if memory else None,
                                        'added_default_bits': sum((a & ~b).bit_count() for a, b in zip(matrix, baseline)) if baseline else None,
                                        'missing_default_bits': sum((b & ~a).bit_count() for a, b in zip(matrix, baseline)) if baseline else None}
                        if name in observed:
                            assert observations == observed[name], (label, 'non-reproducible observations')
                        observed[name] = observations
                        if iteration:
                            timings[name].append(result['seconds'])
                            raw = profile.read_bytes()
                            measured = json.loads(raw)
                            passes = {}
                            for item in measured['passes']:
                                total = passes.setdefault(item['name'], {'calls': 0, 'items': 0, 'seconds': 0, 'seconds_with_setup': 0})
                                total['calls'] += 1
                                for field in ('items', 'seconds', 'seconds_with_setup'):
                                    total[field] += item[field]
                            profiles[name].append({'filename': profile.name, 'sha256': hashlib.sha256(raw).hexdigest(),
                                                   'total_seconds': measured['total_seconds'], 'passes': passes})
                        print(f'{fixture} {mode} {workers} {iteration} {name}: {result["seconds"]:.4f}s', flush=True)
                for name, exe in methods:
                    records.append({'fixture': fixture, 'mode': mode, 'workers': workers, 'implementation': name,
                                    'compiler': str(exe), 'compiler_sha256': hashes[name],
                                    'seconds': timings[name], 'median_seconds': statistics.median(timings[name]),
                                    'observations': observed[name], 'profiles': profiles[name]})
    for name, exe in methods:
        assert hashlib.sha256(exe.read_bytes()).hexdigest() == hashes[name], (name, 'binary changed')
    report = {'schema_version': 1, 'kind': 'whole_process_vis_portal_compaction',
              'timestamp_utc': datetime.now(timezone.utc).isoformat(), 'platform': platform.platform(),
              'processor': platform.processor(), 'logical_cpus': os.cpu_count(),
              'grid': args.grid, 'warmup_runs': 1, 'measured_runs': args.repeat,
              'notes': ['Alternating binaries; only compact/uncompressed parity is required.',
                        'Process startup, input, merge, VIS and output included; no LIGHT-stage timing.',
                        'Visibility costs cover every runtime cluster and unique leaf-referenced world surfaces.',
                        'Triangle costs use compiled indices (no patches in this corpus); no renderer/FPS measurement.',
                        'Passage allocation is an exact requested-byte estimate, not peak process memory.'],
              'fixtures': fixtures, 'records': records}
    (root/'benchmark.json').write_text(json.dumps(report, indent=2)+'\n')
    print('VIS bytes match the uncompressed solver for every mode, sample and worker count')


if __name__ == '__main__':
    main()

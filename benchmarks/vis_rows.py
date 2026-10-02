"""Alternating whole-VIS measurements for distinct-row assembly.
SPDX-License-Identifier: GPL-3.0-or-later.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import statistics
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tests'))
from integration import Bsp, run
from vis_fixtures import create_vis_fixture, create_subdivided_corridor
from vis_row_assembly import carrier, components


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--reference', type=Path, required=True)
    p.add_argument('--work-dir', type=Path, required=True)
    p.add_argument('--repeat', type=int, default=5)
    p.add_argument('--workers', type=int, nargs='+', default=[1,4])
    args = p.parse_args()
    if args.repeat < 2:
        p.error('At least two measured repetitions are required')
    methods = [('reference', args.reference.resolve()), ('current', args.compiler.resolve())]
    root = args.work_dir.resolve()
    report = dict(schema_version=1, recorded_utc=datetime.now(timezone.utc).isoformat(),
                  platform=platform.platform(), warmup_runs=1, measured_runs=args.repeat,
                  compilers={name:dict(path=str(exe), sha256=sha(exe)) for name,exe in methods}, cases=[])
    for fixture in ('chain-2048', 'corridor-512', 'grid-9'):
        directory = root/fixture
        if fixture == 'chain-2048':
            base, target, original = carrier(args.compiler.resolve(), directory)
            text, _ = components([2048])
            target.with_suffix('.prt').write_text(text)
        else:
            source = create_subdivided_corridor(directory) if fixture == 'corridor-512' else create_vis_fixture(directory, grid=9)
            base = ['-game', 'quake3', '-fs_basepath', directory, '-fs_homepath', directory/'home']
            run(args.compiler.resolve(), [*base, '-threads', 1, '-meta', source], directory, 'bsp')
            assert not source.with_suffix('.lin').exists()
            target = source.with_suffix('.bsp')
            original = Bsp(target)
        portals = target.with_suffix('.prt').read_bytes()
        for merge in ((True,) if fixture != 'grid-9' else (False,True)):
            expected = None
            for workers in args.workers:
                results = {name:dict(seconds=[], row_seconds=[], profiles=[]) for name,_ in methods}
                for iteration in range(args.repeat+1):
                    for name, exe in (methods if iteration%2==0 else list(reversed(methods))):
                        target.write_bytes(original.data)
                        label = f'{merge}-{workers}-{iteration}-{name}'
                        profile = directory/(label+'.json')
                        timing = run(exe, [*base, '-threads', workers, '-profile', profile, '-vis', '-reproducible',
                                           '-saveprt', *(['-merge'] if merge else []), target], directory, label, timeout=300)
                        output = Bsp(target)
                        if expected is None:
                            expected = output.lump(16)
                        assert output.lump(16) == expected, (label, 'VIS bytes differ')
                        for lump in range(1,16):
                            assert output.lump(lump) == original.lump(lump), (label,lump)
                        assert target.with_suffix('.prt').read_bytes() == portals
                        clusters, width = struct.unpack_from('<ii', expected)
                        if fixture != 'grid-9':
                            assert all(int.from_bytes(expected[8+i*width:8+(i+1)*width], 'little') == (1<<clusters)-1
                                       for i in range(clusters)), 'Unobstructed/connected fixture visibility differs'
                        metrics = json.loads(profile.read_text())
                        assembly = [item for item in metrics['passes'] if item['name'] == 'AssembleVisRows']
                        if name == 'current':
                            assert len(assembly) == 1
                            results[name]['distinct_rows'] = assembly[0]['items']
                        if iteration:
                            results[name]['seconds'].append(timing['seconds'])
                            results[name]['row_seconds'].append(sum(item['seconds_with_setup'] for item in assembly) if assembly else None)
                            results[name]['profiles'].append(dict(path=str(profile.relative_to(root)), sha256=sha(profile)))
                for values in results.values():
                    values['median_seconds'] = statistics.median(values['seconds'])
                ratio = results['reference']['median_seconds']/results['current']['median_seconds']
                report['cases'].append(dict(fixture=fixture, spatially_matched=fixture!='chain-2048', merge=merge,
                                           clusters=clusters, workers=workers, visibility_sha256=hashlib.sha256(expected).hexdigest(),
                                           input_bsp_sha256=hashlib.sha256(original.data).hexdigest(),
                                           input_prt_sha256=hashlib.sha256(portals).hexdigest(), results=results,
                                           speedup=ratio, reference_and_worker_byte_parity=True))
                print(f'{fixture} merge={merge} workers={workers}: {results["reference"]["median_seconds"]:.4f}s -> '
                      f'{results["current"]["median_seconds"]:.4f}s ({ratio:.2f}x)', flush=True)
    for name,exe in methods:
        assert sha(exe) == report['compilers'][name]['sha256'], 'Compiler changed during measurement'
    report['notes'] = [
        'Alternating binaries; one warmup then all measured samples retained. Whole VIS command includes startup, loading, merging, solving and output.',
        'No BSP generation, LIGHT, renderer or peak memory timing. New row-job timing excludes forest preparation and histogram reduction, which remain in whole-command time.',
        'Chain uses a synthetic PRT/native carrier to isolate merge depth; it is not spatially matched geometry.',
        'Corridor is sealed native source with 512 ordinary 8-unit block cuts, deliberately stressing redundant subdivisions.',
        'Grid is the existing structural 81-pillar workload; ordinary and combined merge selections are compared separately.',
        'Results do not enable or qualify automatic topology changes.']
    (root/'benchmark.json').write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    main()

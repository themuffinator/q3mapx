"""Whole-command cost of consensus UV recovery, with native MAP structure checks.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import re
import statistics
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tests'))
from fixtures import create_fixture
from integration import run
from uv_recovery import reference_uv_policy


def digest(data):
    return hashlib.sha256(data).hexdigest()


def structure(data):
    result = []
    for line in data.splitlines():
        if line.lstrip().startswith(b'(') and b'[' in line:
            fields = line.split()
            assert fields[0] == b'(' and fields[15] != b'[' and fields[16] == b'['
            result.append(b' '.join(fields[:16]+fields[-3:]))
        elif line.lstrip().startswith(b'( ('):
            # Patch rows: compare parsed binary32 positions, allowing the new
            # decimal spelling and treating patch UVs as texture definitions.
            points = re.findall(rb'\(\s*([^()]+)\)', line)
            assert points and all(len(point.split()) == 5 for point in points)
            result.append(b'patch-row '+b''.join(struct.pack('<3f',*map(float,point.split()[:3])) for point in points))
        else: result.append(line)
    return b'\n'.join(result)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--reference', type=Path)
    p.add_argument('--work-dir', type=Path, required=True)
    p.add_argument('--map', type=Path)
    p.add_argument('--game', default='quake3')
    p.add_argument('--game-root', type=Path)
    p.add_argument('--repeat', type=int, default=5)
    a = p.parse_args()
    if a.repeat < 3: p.error('Use at least three measured runs')
    if a.map and not a.game_root: p.error('Native input requires an explicit read-only asset root')
    if not a.map and (a.game != 'quake3' or a.game_root): p.error('Generated fixture requires quake3 and generated assets')
    compiler, root = a.compiler.resolve(), a.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    compiler_hash = digest(compiler.read_bytes())
    if a.map:
        original = a.map.resolve(); assets = a.game_root.resolve(); kind = 'private_native'
    else:
        assets = root/'assets'; source = create_fixture(assets, dense=True, grid=47)
        run(compiler,['-game','quake3','-fs_basepath',assets,'-fs_homepath',root/'home','-threads',1,'-meta',source],root,'prepare',timeout=300)
        original = source.with_suffix('.bsp'); kind = 'generated_47x47_room'
    native = original.read_bytes(); path = root/'input.bsp'; output = root/'recovered.map'
    if original in (path, output): p.error('Use an independent output directory')
    path.write_bytes(native)
    arguments = ['-game',a.game,'-fs_basepath',assets,'-fs_homepath',root/'home','-threads',1,
                 '-decompile','-format','map_220','-o',output]
    old_map = None
    if a.reference:
        reference_options = reference_uv_policy(a.reference.resolve(),root)
        run(a.reference.resolve(),[*arguments,*reference_options,path],root,'reference',timeout=300)
        old_map = output.read_bytes()
    expected, reports, measurements = {}, {}, []
    for iteration in range(a.repeat+1):
        policies = ['triangle','consensus'] if iteration % 2 == 0 else ['consensus','triangle']
        for policy in policies:
            elapsed = run(compiler,[*arguments,'-uv-policy',policy,path],root,f'{policy}-{iteration}',timeout=300)
            result = output.read_bytes()
            report = json.loads(Path(str(output)+'.recovery.json').read_text())
            if policy not in expected: expected[policy], reports[policy] = result, report
            assert result == expected[policy] and report == reports[policy], (policy,'nondeterministic output')
            if policy == 'triangle' and old_map is not None: assert result == old_map, 'Legacy MAP bytes changed'
            measurements.append({'policy':policy,'warmup':iteration==0,**elapsed})
    assert structure(expected['triangle']) == structure(expected['consensus']), 'Non-UV MAP content changed'
    assert original.read_bytes() == native and path.read_bytes() == native
    assert digest(compiler.read_bytes()) == compiler_hash
    medians = {policy:statistics.median(item['seconds'] for item in measurements if item['policy']==policy and not item['warmup'])
               for policy in expected}
    record = {'schema_version':1,'recorded_utc':datetime.now(timezone.utc).isoformat(),
              'kind':kind,'game':a.game,'platform':platform.platform(),'compiler_sha256':compiler_hash,
              'reference_sha256':digest(a.reference.read_bytes()) if a.reference else None,
              'source_sha256':digest(native),'source_bytes':len(native),'sources_preserved':True,
              'triangle_reference_map_parity':old_map is not None,
              'non_uv_map_content_identical':True,'policy_repeatability':True,
              'map_sha256':{k:digest(v) for k,v in expected.items()},
              'faces':reports['consensus']['faces'],'uv_recovery_counts':reports['consensus']['uv_recovery']['counts'],
              'omitted_face_records':reports['consensus']['uv_recovery']['omitted_records'],
              'median_seconds':medians,'consensus_over_triangle_ratio':medians['consensus']/medians['triangle'],
              'measurements':measurements,
              'limits':['Whole-command overhead measurement; not a speedup or parallel scaling claim.',
                        'Native author UVs are unavailable; unchanged non-UV text and fit residuals do not prove original-map fidelity.']}
    (root/'benchmark.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps({k:record[k] for k in ('game','faces','uv_recovery_counts','median_seconds','consensus_over_triangle_ratio')},indent=2))


if __name__ == '__main__': main()

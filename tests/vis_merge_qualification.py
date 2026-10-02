"""Audit lost baseline VIS bits against exact geometry of matched source fixtures.

An unresolved geometric pair makes this optional qualification command fail.
This does not change or automatically authorize any compiler merge.
SPDX-License-Identifier: GPL-3.0-or-later.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from integration import Bsp, run
from vis_fixtures import create_vis_fixture
from vis_merge import rows
from vis_occlusion import certify_occlusion, replay_certificate


def cells_from_tree(bsp, extent):
    # Reconstruct exact path boxes, independent of stored leaf bounds and of
    # the compiler's floating cell/portal analyzers. This fixture is axial.
    planes = list(struct.iter_unpack('<4f', bsp.lump(2)))
    nodes = list(struct.iter_unpack('<9i', bsp.lump(3)))
    leaves = list(struct.iter_unpack('<12i', bsp.lump(4)))
    result = {}
    def visit(node, lo, hi):
        if any(a >= b for a, b in zip(lo, hi)):
            return
        if node < 0:
            cluster = leaves[-1-node][0]
            if cluster >= 0:
                result.setdefault(cluster, []).append((lo, hi))
            return
        plane, front, back, *_ = nodes[node]
        normal, distance = planes[plane][:3], planes[plane][3]
        assert sum(bool(v) for v in normal) == 1
        axis = next(i for i in range(3) if normal[i])
        assert abs(normal[axis]) == 1 and distance.is_integer()
        cut = int(distance/normal[axis])
        low, high = list(lo), list(hi)
        low[axis], high[axis] = max(lo[axis], cut), min(hi[axis], cut)
        if normal[axis] < 0:
            front, back = back, front
        visit(front, low, hi)
        visit(back, lo, high)
    visit(0, [-extent, -extent, 0], [extent, extent, 256])
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--work-dir', type=Path, required=True)
    p.add_argument('--workers', type=int, nargs='+', default=[1, 4])
    args = p.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    limits = dict(work=2_000_000, states=2048, depth=10, refinement=12)
    report = dict(schema_version=2, compiler_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  contract='Exact interior-occlusion proof for axial known-source fixtures; zero-area grazing rays excluded; no automatic compiler edits.',
                  oracle_limits=limits,
                  grids=[], qualified=False)
    for grid in (5, 9):
        directory = root/f'grid-{grid}'
        source = create_vis_fixture(directory, grid=grid)
        base = ['-game', 'quake3', '-fs_basepath', directory, '-fs_homepath', directory/'home']
        run(exe, [*base, '-threads', 1, '-meta', source], directory, 'bsp')
        target = source.with_suffix('.bsp')
        original = Bsp(target)
        portals = source.with_suffix('.prt').read_bytes()
        assert not source.with_suffix('.lin').exists(), 'Fixture leaked'
        cells = cells_from_tree(original, (grid//2+1)*128)
        limit = grid//2*128
        # Original first-party fixture solids, not inferred leaf bounding boxes
        # or the PVS under test. The inline door is deliberately not an occluder.
        solids = [((x-20, y-20, 0), (x+20, y+20, 80+(x+y) % 112))
                  for y in range(-limit, limit+1, 128) for x in range(-limit, limit+1, 128)]
        record = dict(grid=grid, clusters=len(cells), cases=[], pairs=[],
                      opaque_source_boxes=solids,
                      source_sha256_lf=hashlib.sha256(source.read_text().encode()).hexdigest(),
                      input_bsp_sha256=hashlib.sha256(original.data).hexdigest(),
                      prt_sha256=hashlib.sha256(portals).hexdigest())
        pairs = set()
        for solver, flags in [('full', []), ('portal', ['-nopassage']), ('passage', ['-passageOnly']), ('fast', ['-fast'])]:
            control = None
            for mode, options in [('original', []), ('merge', ['-merge']), ('polygons', ['-mergeportals']), ('merge-nosort', ['-merge', '-nosort'])]:
                expected = None
                for workers in args.workers:
                    label = f'{solver}-{mode}-{workers}'
                    target.write_bytes(original.data)
                    timing = run(exe, [*base, '-threads', workers, '-vis', '-reproducible', '-saveprt', *flags, *options, source], directory, label, timeout=300)
                    output = Bsp(target)
                    for lump in range(1, 16):
                        assert output.lump(lump) == original.lump(lump), (label, lump)
                    assert source.with_suffix('.prt').read_bytes() == portals
                    matrix = rows(output)
                    assert len(matrix) == len(cells)
                    assert all(r & (1 << i) and r < (1 << len(matrix)) for i, r in enumerate(matrix))
                    if expected is None:
                        expected = output.lump(16)
                    assert output.lump(16) == expected, (label, 'worker-dependent VIS')
                    if control is None:
                        control = matrix
                    missing = [(i, j) for i, (before, after) in enumerate(zip(control, matrix))
                               for j in range(len(matrix)) if (before & ~after) & (1 << j)]
                    pairs.update(missing)
                    record['cases'].append(dict(solver=solver, mode=mode, workers=workers, seconds=timing['seconds'],
                        missing_baseline_pairs=missing, added_baseline_bits=sum((a & ~b).bit_count() for a, b in zip(matrix, control)),
                        visible_pairs=sum(r.bit_count() for r in matrix), visibility_sha256=hashlib.sha256(output.lump(16)).hexdigest()))
        for left, right in sorted(pairs):
            proofs = []
            for a in cells[left]:
                for b in cells[right]:
                    result = certify_occlusion(a, b, solids, **limits)
                    replayed = False
                    if result['status'] == 'occluded_interiors':
                        replayed = replay_certificate(a, b, solids, result['certificate'])
                        assert replayed, (grid, left, right, 'Independent replay rejected certificate')
                    proofs.append(dict(source_box=a, target_box=b, replay_verified=replayed, **result))
            proven = all(p['status'] == 'occluded_interiors' and p['replay_verified'] for p in proofs)
            record['pairs'].append(dict(clusters=[left, right], proven_occluded_interiors=proven, cell_pairs=proofs))
        record['unresolved_pairs'] = [p['clusters'] for p in record['pairs'] if not p['proven_occluded_interiors']]
        report['grids'].append(record)
        (directory/'qualification.json').write_text(json.dumps(record, indent=2)+'\n', encoding='utf-8')
        print(f'grid {grid}: {len(record["cases"])} native runs, {len(pairs)} distinct lost pairs, {len(record["unresolved_pairs"])} unresolved', flush=True)
    report['qualified'] = all(not g['unresolved_pairs'] for g in report['grids'])
    (root/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    if not report['qualified']:
        raise SystemExit('Merge qualification remains unresolved; see validation.json. No automatic transformation is authorized.')
    print('Known-source interior-occlusion qualification passed; arbitrary maps and renderer boundaries remain unqualified.')


if __name__ == '__main__':
    main()

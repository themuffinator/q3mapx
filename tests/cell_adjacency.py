"""BSP-only cell adjacency checked against independently compiled PRT geometry.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess

from integration import Bsp, run
from native_early import native_lumps as early_lumps, pack_file
from native_fakk import native_file as fakk_file
from native_mohaa import native_lumps as mohaa_lumps, pack as mohaa_pack
from portal_evidence import bsp_lumps, prt_graph, replace_lump
from vis_fixtures import create_round_vis_fixture, create_vis_fixture


def subtract(a, b):
    return tuple(x-y for x, y in zip(a, b))


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def polygon_area(points):
    return sum(math.sqrt(dot(c, c))/2 for c in
               (cross(subtract(points[i], points[0]), subtract(points[i+1], points[0]))
                for i in range(1, len(points)-1)))


def containment_error(points, polygon):
    """Independent convex edge/plane membership, insensitive to winding order."""
    center = tuple(sum(p[a] for p in polygon)/len(polygon) for a in range(3))
    normals = [cross(subtract(polygon[i], polygon[0]), subtract(polygon[i+1], polygon[0]))
               for i in range(1, len(polygon)-1)]
    normal = max(normals, key=lambda n: dot(n, n))
    length = math.sqrt(dot(normal, normal))
    if not length:
        return math.inf
    normal = tuple(v/length for v in normal)
    error = max(abs(dot(subtract(p, polygon[0]), normal)) for p in points)
    for a, b in zip(polygon, polygon[1:]+polygon[:1]):
        edge = cross(subtract(b, a), normal)
        length = math.sqrt(dot(edge, edge))
        if length < 1e-8:
            continue
        sign = 1 if dot(subtract(center, a), edge) >= 0 else -1
        error = max(error, max(-sign*dot(subtract(p, a), edge)/length for p in points))
    return error


def verify(graph, report, path, prt, *, rounded):
    assert graph['status'] == 'reconstructed'
    assert not graph['original_prt_recovered'] and not graph['author_classification_proven']
    assert math.isclose(graph['enclosure_volume'], graph['summed_cell_volume'], rel_tol=1e-8)
    # Path halfspaces from the native tree are the membership oracle. Stored
    # leaf AABBs and the report's own bounds cannot establish these memberships.
    lumps = bsp_lumps(path)
    nodes = list(struct.iter_unpack('<9i', lumps[3]))
    planes = list(struct.iter_unpack('<4f', lumps[2]))
    leaves = list(struct.iter_unpack('<12i', lumps[4]))
    paths = defaultdict(list)
    pending = [(report['world_graph']['head'], [])]
    while pending:
        index, constraints = pending.pop()
        if index < 0:
            paths[-1-index].append(constraints)
        else:
            node = nodes[index]
            for side, child in enumerate(node[1:3]):
                pending.append((child, constraints+[(index, side)]))

    def inside(point, constraints, tolerance):
        return all((1 if side == 0 else -1)*(dot(planes[nodes[n][0]][:3], point)-planes[nodes[n][0]][3])
                   >= -tolerance for n, side in constraints)

    cell_paths = []
    for index, cell in enumerate(graph['cells']):
        assert cell['index'] == index and cell['volume'] > 0
        assert cell['cluster'] == leaves[cell['leaf']][0]
        constraints = [p for p in paths[cell['leaf']] if inside(cell['interior_point'], p, 1e-7)]
        assert len(constraints) == 1, (index, cell['leaf'], len(constraints))
        cell_paths.append(constraints[0])
        for axis in range(3):
            assert graph['enclosure']['mins'][axis] <= cell['bounds']['mins'][axis]+1e-7
            assert cell['bounds']['maxs'][axis] <= graph['enclosure']['maxs'][axis]+1e-7
            assert cell['bounds']['mins'][axis] < cell['interior_point'][axis] < cell['bounds']['maxs'][axis]

    text = prt.read_text(encoding='utf-8')
    clusters, portals, count = prt_graph(text)
    expected = defaultdict(list)
    for a, b, _, points in portals:
        expected[('between_clusters', *sorted((a, b)))].append(points)
    for line in text.splitlines()[4+len(portals):]:
        n, cluster = map(int, line[:line.index('(')].split())
        points = [tuple(map(float, p.split())) for p in re.findall(r'\(([^)]+)\)', line)]
        assert len(points) == n
        expected[('open_opaque', cluster)].append(points)
    assert sum(len(v) for k, v in expected.items() if k[0] == 'open_opaque') == count
    observed = defaultdict(list)
    for face in graph['interfaces']:
        a, b = face['front_cell'], face['back_cell']
        assert a != b and (face['partition_node'], 0) in cell_paths[a]
        assert (face['partition_node'], 1) in cell_paths[b]
        assert all(inside(p, cell_paths[a], 2e-6) and inside(p, cell_paths[b], 2e-6) for p in face['points'])
        assert math.isclose(polygon_area(face['points']), face['area'], rel_tol=1e-7, abs_tol=1e-7)
        ca, cb = (graph['cells'][i]['cluster'] for i in (a, b))
        key = ('between_clusters', *sorted((ca, cb))) if ca >= 0 and cb >= 0 else ('open_opaque', max(ca, cb))
        assert key[0] == face['kind'] and max(ca, cb) >= 0
        observed[key].append(face['points'])
    assert observed.keys() == expected.keys()
    matches, max_area_error, max_boundary_error = 0, 0, 0
    # The compiler's float PRT construction and its serialized BSP planes are
    # distinct numerical paths. The oblique fixture exposes differences, so
    # keep their tolerance separate from the strict native-path checks above.
    boundary_limit, area_limit = (0.1, 0.01) if rounded else (0.0001, 1e-7)
    for key, polygons in observed.items():
        reference = expected[key].copy()
        assert len(polygons) == len(reference), (key, len(polygons), len(reference))
        for polygon in polygons:
            compatible = []
            for i, other in enumerate(reference):
                area_error = abs(polygon_area(polygon)-polygon_area(other))/polygon_area(other)
                boundary_error = max(containment_error(polygon, other), containment_error(other, polygon))
                if area_error <= area_limit and boundary_error <= boundary_limit:
                    compatible.append(i)
                    max_area_error = max(max_area_error, area_error)
                    max_boundary_error = max(max_boundary_error, boundary_error)
            assert len(compatible) == 1, (key, polygon, compatible)
            del reference[compatible[0]]
            matches += 1
    assert matches == len(portals)+count == len(graph['interfaces'])
    assert not graph['open_faces_on_enclosure'], 'Sealed fixture touched the artificial enclosure'
    return {'clusters': clusters, 'cells': len(graph['cells']), 'interfaces': matches,
            'interface_kinds': dict(Counter(f['kind'] for f in graph['interfaces'])),
            'degenerate_fragments': graph['degenerate_fragments'],
            'enclosure_volume': graph['enclosure_volume'],
            'prt_comparison': {'boundary_tolerance_units': boundary_limit, 'relative_area_tolerance': area_limit,
                               'max_plane_edge_deviation_units': max_boundary_error, 'max_relative_area_error': max_area_error},
            'source_map_sha256_lf': hashlib.sha256(path.with_suffix('.map').read_text(encoding='utf-8').encode()).hexdigest(),
            'source_prt_sha256_lf': hashlib.sha256(text.encode()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--grid', type=int, default=3)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    records, failures, native_profiles = [], [], []

    def analyze(path, label, base, *, extra=(), workers=1):
        original = path.read_bytes()
        output = root/(label+'.json')
        run(exe, [*base, '-threads', workers, '-bsp-evidence', '-cell-adjacency',
                  '-report', output, *extra, path], root, label, timeout=60)
        assert path.read_bytes() == original
        report = json.loads(output.read_text(encoding='utf-8'))
        assert report['source']['sha256'] == hashlib.sha256(original).hexdigest()
        return report

    def failure(path, label, base, *, extra=(), output=None, message):
        output = output or root/'preserved.json'
        if output != path and not output.is_dir():
            output.write_bytes(b'previous report')
        original = path.read_bytes()
        previous = output.read_bytes() if output.is_file() else None
        result = subprocess.run([str(exe), *map(str, [*base, '-bsp-evidence', '-cell-adjacency',
            '-report', output, *extra, path])], cwd=root, capture_output=True, timeout=30)
        log = result.stdout+result.stderr
        (root/(label+'.log')).write_bytes(log)
        assert result.returncode == 1 and message.lower() in log.decode(errors='replace').lower(), (label, log)
        assert path.read_bytes() == original
        assert (output.read_bytes() == previous) if previous is not None else output.is_dir()
        assert not list(root.rglob('*.q3mapx-*.tmp'))
        failures.append(label)

    for game, detail, rounded in [('quake3', False, False), ('quake3', True, False),
                                  ('ja', False, False), ('ja', True, False), ('quake3', False, True)]:
        name = game+('-round' if rounded else '-detail' if detail else '-structural')
        directory = root/name
        source = create_round_vis_fixture(directory) if rounded else create_vis_fixture(directory, grid=args.grid, detail=detail)
        base = ['-game', game, '-fs_basepath', directory, '-fs_basegame', 'baseq3', '-fs_homepath', root/'home']
        run(exe, [*base, '-threads', 2, '-meta', source], directory, 'bsp')
        path, prt = source.with_suffix('.bsp'), source.with_suffix('.prt')
        original_prt = prt.read_bytes()
        initial = analyze(path, name+'-no-vis', base)
        graph = initial['cell_adjacency']
        record = verify(graph, initial, path, prt, rounded=rounded)
        run(exe, [*base, '-threads', 2, '-vis', '-reproducible', '-saveprt', source], directory, 'vis')
        report = analyze(path, name, base)
        assert report['cell_adjacency'] == graph
        assert analyze(path, name+'-threads4', base, workers=4) == report
        paired = analyze(path, name+'-with-prt', base, extra=['-portals', prt])
        assert paired['cell_adjacency'] == graph and 'portal_analysis' in paired
        assert prt.read_bytes() == original_prt
        records.append({'game': game, 'detail': detail, 'rounded': rounded, **record})
        if detail or rounded:
            continue
        combined = analyze(path, name+'-brush-cells', base, extra=['-brush-cells', '-portals', prt])
        assert combined['cell_adjacency'] == graph and combined['portal_analysis'] == paired['portal_analysis']
        budget = combined['limits']['work_units_used']
        exact = analyze(path, name+'-exact-budget', base, extra=['-brush-cells', '-portals', prt, '-max-work', budget])
        assert exact['limits']['work_units_used'] == budget and exact['cell_adjacency'] == graph
        failure(path, name+'-shared-budget', base, extra=['-brush-cells', '-portals', prt, '-max-work', budget-1], message='budget')
        budget = report['limits']['work_units_used']
        failure(path, name+'-cell-budget', base, extra=['-max-work', budget-1], message='budget')
        mutated = directory/'mutated.bsp'
        original = path.read_bytes()
        lumps = bsp_lumps(path)
        # These conservative stored AABBs overlap everywhere. Actual adjacency
        # must still follow the clipping planes and node paths.
        leaves = bytearray(lumps[4])
        for offset in range(0, len(leaves), 48):
            struct.pack_into('<6i', leaves, offset+8, -65536, -65536, -65536, 65536, 65536, 65536)
        mutated.write_bytes(replace_lump(original, 4, leaves))
        assert analyze(mutated, name+'-leaf-boxes', base)['cell_adjacency'] == graph
        # Repeated native leaf references are geometric cells, not duplicate
        # entries to discard. Cut the enclosure through its center.
        leaves = list(struct.iter_unpack('<12i', lumps[4]))
        leaf = next(i for i, value in enumerate(leaves) if value[0] == 0)
        nodes = bytearray(lumps[3])
        plane = struct.unpack_from('<i', nodes)[0]
        struct.pack_into('<ii', nodes, 4, -1-leaf, -1-leaf)
        planes = bytearray(lumps[2])
        center = (graph['enclosure']['mins'][0]+graph['enclosure']['maxs'][0])/2
        struct.pack_into('<4f', planes, 16*plane, 1, 0, 0, center)
        mutated.write_bytes(replace_lump(replace_lump(original, 3, nodes), 2, planes))
        shared_leaf = analyze(mutated, name+'-shared-leaf', base)['cell_adjacency']
        assert shared_leaf['status'] == 'reconstructed' and len(shared_leaf['cells']) == 2
        assert {c['leaf'] for c in shared_leaf['cells']} == {leaf}
        assert len(shared_leaf['interfaces']) == 1 and shared_leaf['interfaces'][0]['kind'] == 'within_cluster'
        assert shared_leaf['open_faces_on_enclosure'] == 10
        nodes = bytearray(lumps[3])
        child = next(c for c in struct.unpack_from('<ii', nodes, 4) if c >= 0)
        struct.pack_into('<ii', nodes, 4, child, child)
        mutated.write_bytes(replace_lump(original, 3, nodes))
        unavailable = analyze(mutated, name+'-shared-node', base)['cell_adjacency']
        assert unavailable['status'] == 'world_tree_unavailable' and not unavailable['cells'] and not unavailable['interfaces']
        # The native loader accepts finite model bounds outside our bounded
        # geometry domain; this stage must reject them and preserve prior output.
        models = bytearray(lumps[7])
        struct.pack_into('<f', models, 0, -20_000_000)
        mutated.write_bytes(replace_lump(original, 7, models))
        failure(mutated, name+'-enclosure', base, message='enclosure')
        protected = directory/'input.json'
        protected.write_bytes(original)
        failure(protected, name+'-protect-input', base, output=protected, message='must not replace')
        blocked = directory/'directory.json'
        blocked.mkdir(exist_ok=True)
        failure(path, name+'-publish-directory', base, output=blocked, message='regular file')

    # Native recovery-only readers normalize the same world planes/tree. None
    # of these controls needs proprietary geometry, assets or a native writer.
    carrier = Bsp(root/'quake3-structural/baseq3/maps/fixture.bsp')
    reference = json.loads((root/'quake3-structural.json').read_text(encoding='utf-8'))['cell_adjacency']
    profiles = [('alice', fakk_file(carrier, 42)), ('fakk2', fakk_file(carrier, 12)),
                ('mohaa', mohaa_pack(mohaa_lumps(carrier)))]
    profiles += [(game, pack_file(v, early_lumps(carrier, v)))
                 for v, game in ((43, 'q3-ihv'), (44, 'q3test44'), (45, 'q3test45'))]
    for game, data in profiles:
        path = root/(game+'.bsp')
        path.write_bytes(data)
        base = ['-game', game, '-fs_basepath', root, '-fs_homepath', root/'home']
        assert analyze(path, 'native-'+game, base)['cell_adjacency'] == reference
        native_profiles.append(game)
    early = early_lumps(carrier, 44)
    nodes = bytearray(early[2])
    for offset in range(0, len(nodes), 36):
        for at in (offset+4, offset+8):
            child = struct.unpack_from('<i', nodes, at)[0]
            if child >= 0:
                struct.pack_into('<i', nodes, at, child+1)
    early[2] = struct.pack('<9i', 0, -1, -1, -128, -128, -128, 128, 128, 128)+nodes
    models = bytearray(early[6])
    for offset in range(0, len(models), 48):
        head = struct.unpack_from('<i', models, offset+36)[0]
        if head >= 0:
            struct.pack_into('<i', models, offset+36, head+1)
    early[6] = models
    path = root/'relocated-world-head.bsp'
    path.write_bytes(pack_file(44, early))
    base = ['-game', 'q3test44', '-fs_basepath', root, '-fs_homepath', root/'home']
    relocated = analyze(path, 'native-relocated-head', base)
    assert relocated['world_graph']['head'] == 1
    for face in relocated['cell_adjacency']['interfaces']:
        face['partition_node'] -= 1
    assert relocated['cell_adjacency'] == reference
    leaf = next(c['leaf'] for c in reference['cells'] if c['cluster'] == 0)
    struct.pack_into('<i', models, 36, -1-leaf)
    path.write_bytes(pack_file(44, early))
    single = analyze(path, 'native-single-leaf', base)['cell_adjacency']
    assert single['status'] == 'reconstructed' and len(single['cells']) == 1 and not single['interfaces']
    assert single['cells'][0]['leaf'] == leaf and single['open_faces_on_enclosure'] == 6

    assert not list(root.rglob('*.q3mapx-*.tmp'))
    (root/'validation.json').write_text(json.dumps({'schema_version': 1,
        'compiler_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'known_source_pairs': records,
        'native_recovery_only_profiles': native_profiles,
        'failed_commands_with_preserved_input_and_output': failures,
        'checks': ['Native path-halfspace membership for every interface vertex and cell interior',
                   'PRT one-to-one polygon, endpoint, area and bidirectional containment oracle',
                   'IBSP/RBSP structural and manual-detail pairs', '64-sided oblique corridor',
                   'No PRT required; unchanged graph before/after VIS or with supplied PRT',
                   'Exact 1/4-worker report parity', 'Combined brush-cell and PRT evidence with shared work budget',
                   'Exact/exhausted budgets, source/output preservation and clean staging',
                   'Stored leaf bounds independence', 'Repeated leaf path cells and exposed enclosure faces',
                   'Shared internal-node abstention', 'Enclosure coordinate and publication failures',
                   'Six native recovery-only readers with exact normalized graph equality',
                   'Relocated early-format world head and single-leaf world'],
        'result': 'passed'}, indent=2)+'\n', encoding='utf-8')
    print('Native path, matched PRT geometry, detail pairs, oblique cuts and protected failure controls passed')


if __name__ == '__main__':
    main()

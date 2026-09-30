"""Independent graph/PVS oracles for regional portal diagnostics. GPL-3.0-or-later."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess

from integration import run
from vis_fixtures import create_vis_fixture


def bsp_lumps(path):
    data = path.read_bytes()
    assert data[:4] in (b'IBSP', b'RBSP')
    count = 18 if data[:4] == b'RBSP' else 17
    return [data[a:a+b] for a, b in (struct.unpack_from('<ii', data, 8+i*8) for i in range(count))]


def prt_graph(text):
    lines = text.splitlines()
    clusters, count, faces = map(int, lines[1:4])
    edges = []
    for line in lines[4:4+count]:
        n, a, b, flags = map(int, line[:line.index('(')].split())
        points = [tuple(map(float, p.split())) for p in re.findall(r'\(([^)]+)\)', line)]
        assert len(points) == n
        edges.append((a, b, flags, points))
    return clusters, edges, faces


def replace_lump(data, number, payload):
    result = bytearray(data)
    result.extend(b'\0' * (-len(result) % 4))
    struct.pack_into('<ii', result, 8+number*8, len(result), len(payload))
    result.extend(payload)
    return bytes(result)


def graph_oracle(clusters, edges):
    neighbors = [[] for _ in range(clusters)]
    for i, (a, b, _, _) in enumerate(edges):
        neighbors[a].append((b, i)); neighbors[b].append((a, i))
    def reachable(start, removed=-1):
        seen, pending = {start}, [start]
        while pending:
            for other, edge in neighbors[pending.pop()]:
                if edge != removed and other not in seen:
                    seen.add(other); pending.append(other)
        return seen
    remaining, components = set(range(clusters)), 0
    while remaining:
        remaining -= reachable(min(remaining)); components += 1
    # Independent removal/reachability checks, rather than another low-link DFS.
    bridges = {i for i, (a, b, _, _) in enumerate(edges) if b not in reachable(a, i)}
    degree = [len(n) for n in neighbors]
    return components, bridges, degree


def verify(report, path, prt):
    graph = report['portal_analysis']
    clusters, edges, faces = prt_graph(prt.read_text())
    assert graph['source']['sha256'] == hashlib.sha256(prt.read_bytes()).hexdigest()
    assert not graph['source']['pairing_proven']
    assert (graph['clusters'], graph['portals'], graph['faces']) == (clusters, len(edges), faces)
    components, bridges, degree = graph_oracle(clusters, edges)
    assert graph['components'] == components and graph['bridge_portals'] == len(bridges)
    assert {p['portal'] for p in graph['openings'] if p['graph_bridge']} == bridges
    assert [c['degree'] for c in graph['cluster_costs']] == degree
    pair_count = sum(d*(d-1) for d in degree)
    assert graph['ordered_portal_pairs_upper_bound'] == pair_count
    assert graph['portal_bitset_bytes'] == ((len(edges)*2+63)//64)*8
    assert graph['passage_bitsets_bytes_upper_bound'] == pair_count*graph['portal_bitset_bytes']
    for opening, (_, _, _, points) in zip(graph['openings'],edges):
        if not opening['planar_convex_within_tolerance']:
            assert opening['area'] is None and opening['perimeter'] is None
            continue
        perimeter = sum(math.dist(p,q) for p,q in zip(points,points[1:]+points[:1]))
        area = 0
        for i in range(1,len(points)-1):
            a = [points[i][j]-points[0][j] for j in range(3)]
            b = [points[i+1][j]-points[0][j] for j in range(3)]
            cross = [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
            area += math.sqrt(sum(v*v for v in cross))/2
        assert math.isclose(opening['area'],area,rel_tol=1e-7,abs_tol=1e-5)
        assert math.isclose(opening['perimeter'],perimeter,rel_tol=1e-9)
        assert math.isclose(opening['area_perimeter_width'],2*area/perimeter,rel_tol=1e-7,abs_tol=1e-5)
        assert math.isclose(opening['compactness'],4*math.pi*area/(perimeter*perimeter),rel_tol=1e-7,abs_tol=1e-5)
    assert sum(r['ordered_portal_pairs_upper_bound'] for r in graph['regions']) == pair_count
    assert sorted(c for r in graph['regions'] for c in r['clusters']) == list(range(clusters))
    assert [r['ordered_portal_pairs_upper_bound'] for r in graph['regions']] == sorted(
        (r['ordered_portal_pairs_upper_bound'] for r in graph['regions']), reverse=True)
    for region in graph['regions']:
        members = set(region['clusters'])
        local = [(a,b,f) for a,b,f,_ in edges if a in members or b in members]
        assert region['incident_portals'] == len(local)
        assert region['internal_portals'] == sum(a in members and b in members for a,b,_ in local)
        assert region['boundary_portals'] == sum((a in members) != (b in members) for a,b,_ in local)
        assert region['hint_portals'] == sum(bool(f&1) for _,_,f in local)
        assert region['sky_portals'] == sum(bool(f&2) for _,_,f in local)
        assert region['unknown_flag_portals'] == sum(bool(f&~3) for _,_,f in local)
        openings = [graph['openings'][i] for i,(a,b,_,_) in enumerate(edges) if a in members or b in members]
        assert region['small_portals'] == sum(p['area'] is not None and p['area'] < 64 for p in openings)
        assert region['slender_portals'] == sum(p['compactness'] is not None and p['compactness'] < 0.1 for p in openings)
        assert len(region['associated_world_brush_sample']) <= 64
        for b in region['associated_world_brush_sample']:
            assert report['brushes'][b]['model'] == 0 and report['brushes'][b]['leaf_path_partition_side_indices']
    if graph['world_mapping_available']:
        lumps = bsp_lumps(path)
        leaves = [struct.unpack_from('<12i', lumps[4], i) for i in range(0, len(lumps[4]), 48)]
        nodes = [struct.unpack_from('<9i', lumps[3], i) for i in range(0, len(lumps[3]), 36)]
        region_of_node = {r['node']:r['frontier_index'] for r in graph['regions'] if r['node'] is not None}
        fallback = next(r['frontier_index'] for r in graph['regions'] if r['node'] is None)
        cluster_regions = [set() for _ in range(clusters)]
        pending = [(report['world_graph']['head'], fallback)]
        reachable_leaves = set()
        while pending:
            node, region = pending.pop()
            if node >= 0:
                region = region_of_node.get(node, region)
                pending.extend((child, region) for child in nodes[node][1:3])
            else:
                reachable_leaves.add(-1-node)
                if leaves[-1-node][0] >= 0:
                    cluster_regions[leaves[-1-node][0]].add(region)
        expected_regions = [next(iter(rs)) if len(rs)==1 else fallback for rs in cluster_regions]
        assert [c['frontier_index'] for c in graph['cluster_costs']] == expected_regions
        first, count = struct.unpack_from('<ii', lumps[7], 24)
        world = set(range(first, first+count))
        leaf_surfaces = struct.unpack('<%di' % (len(lumps[5])//4), lumps[5])
        surfaces = [set() for _ in range(clusters)]
        for index in reachable_leaves:
            leaf = leaves[index]
            if leaf[0] >= 0:
                surfaces[leaf[0]].update(set(leaf_surfaces[leaf[8]:leaf[8]+leaf[9]]) & world)
        stride = 148 if path.read_bytes()[:4] == b'RBSP' else 104
        triangles = [struct.unpack_from('<i', lumps[13], i+24)[0]//3 for i in range(0,len(lumps[13]),stride)]
        patches = [struct.unpack_from('<i', lumps[13], i+8)[0]==2 for i in range(0,len(lumps[13]),stride)]
        for c, observed in enumerate(graph['cluster_costs']):
            assert observed['unique_world_surfaces'] == len(surfaces[c])
            assert observed['unique_world_indexed_triangles'] == sum(triangles[s] for s in surfaces[c])
        if graph['stored_pvs_costs_available']:
            n, width = struct.unpack_from('<ii', lumps[16])
            assert n == clusters
            for c, observed in enumerate(graph['cluster_costs']):
                visible = {j for j in range(n) if lumps[16][8+c*width+j//8] & (1 << (j%8))}
                drawn = set().union(*(surfaces[j] for j in visible))
                assert observed['visible_world_surfaces'] == len(drawn)
                assert observed['visible_world_indexed_triangles'] == sum(triangles[s] for s in drawn)
                assert observed['visible_world_patch_surfaces'] == sum(patches[s] for s in drawn)
            for region in graph['regions']:
                members = region['clusters']
                if not members:
                    assert region['stored_internal_pvs_density'] is None
                    continue
                pairs = sum(bool(lumps[16][8+c*width+j//8] & (1 << (j%8))) for c in members for j in members)
                assert region['stored_internal_pvs_density'] == pairs/(len(members)**2)
        else:
            assert all(c['visible_world_indexed_triangles'] is None for c in graph['cluster_costs'])
    return graph


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--grid', type=int, default=3)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    records = []

    def analyze(path, prt, label, base, *, workers=1, extra=()):
        before = path.read_bytes(), prt.read_bytes()
        output = root/(label+'.json')
        run(exe, [*base, '-threads', workers, '-bsp-evidence', '-portals', prt,
                  '-region-depth', 2, '-report', output, *extra, path], root, label)
        report = json.loads(output.read_text())
        assert (path.read_bytes(), prt.read_bytes()) == before
        return report, verify(report,path,prt)

    def failure(path, prt, label, base, extra=(), output=None):
        output = output or root/'preserved.json'
        output.write_bytes(b'previous report') if output != prt else None
        before = path.read_bytes(), prt.read_bytes(), output.read_bytes()
        result = subprocess.run([str(exe), *map(str,[*base,'-bsp-evidence','-portals',prt,
            '-report',output,*extra,path])], cwd=root, capture_output=True, timeout=30)
        (root/(label+'.log')).write_bytes(result.stdout+result.stderr)
        assert result.returncode == 1, (label,result.stdout,result.stderr)
        assert (path.read_bytes(),prt.read_bytes(),output.read_bytes()) == before
        assert not list(root.rglob('*.q3mapx-*.tmp'))

    for game in ('quake3','ja'):
        for detail in (False,True):
            directory = root/(game+('-detail' if detail else '-structural'))
            source = create_vis_fixture(directory,grid=args.grid,detail=detail)
            base = ['-game',game,'-fs_basepath',directory,'-fs_basegame','baseq3','-fs_homepath',root/'home','-threads',2]
            run(exe,[*base,'-meta',source],directory,'bsp')
            path, prt = source.with_suffix('.bsp'), source.with_suffix('.prt')
            _, before = analyze(path,prt,directory.name+'-no-vis',base)
            assert not before['stored_pvs_costs_available']
            run(exe,[*base,'-vis','-reproducible','-saveprt',source],directory,'vis')
            report, graph = analyze(path,prt,directory.name,base)
            threaded, _ = analyze(path,prt,directory.name+'-threads4',base,workers=4)
            assert threaded == report
            assert not graph['pair_probes']['disagree'] and not graph['pair_probes']['unusable_windings'], graph['pair_probes']
            assert graph['pair_probes']['agree'] == graph['portals']
            records.append({'game':game,'detail':detail,'clusters':graph['clusters'],'portals':graph['portals'],
                            'pair_work':graph['ordered_portal_pairs_upper_bound'],'probes':graph['pair_probes']})
            if detail:
                assert records[-1]['pair_work'] < records[-2]['pair_work']
                continue
            exact, _ = analyze(path,prt,directory.name+'-exact-budget',base,extra=['-max-work',report['limits']['work_units_used']])
            assert exact['limits']['work_units_used'] == report['limits']['work_units_used']
            failure(path,prt,directory.name+'-work-limit',base,['-max-work',1])
            failure(path,prt,directory.name+'-portal-work-limit',base,
                    ['-region-depth',2,'-max-work',report['limits']['work_units_used']-1])
            _, combined = analyze(path,prt,directory.name+'-brush-cells',base,extra=['-brush-cells'])
            assert combined == graph

            original = prt.read_text(); lines = original.splitlines(); count = int(lines[2])
            # Stored PVS padding and unused tail bits have no runtime meaning.
            original_bsp = path.read_bytes(); payloads = bsp_lumps(path)
            pvs = bytearray(payloads[16]); n, width = struct.unpack_from('<ii',pvs)
            meaningful = (n+7)//8
            for row in range(n):
                if n%8:
                    pvs[8+row*width+meaningful-1] |= (255 << (n%8)) & 255
                pvs[8+row*width+meaningful:8+(row+1)*width] = b'\xff'*(width-meaningful)
            mutated = directory/'padded-vis.bsp'; mutated.write_bytes(replace_lump(original_bsp,16,pvs))
            _, padded = analyze(mutated,prt,directory.name+'-pvs-padding',base)
            assert padded == graph

            # Shared internal nodes disable path attribution; repeated leaf
            # records are instead deduplicated even after more than 256 paths.
            nodes = bytearray(payloads[3])
            child = next(v for v in struct.unpack_from('<ii',nodes,4) if v >= 0)
            struct.pack_into('<ii',nodes,4,child,child)
            mutated.write_bytes(replace_lump(original_bsp,3,nodes))
            _, shared = analyze(mutated,prt,directory.name+'-shared-node',base)
            assert not shared['world_mapping_available'] and not shared['stored_pvs_costs_available']
            leaves = [struct.unpack_from('<12i',payloads[4],i) for i in range(0,len(payloads[4]),48)]
            leaf = next(i for i,l in enumerate(leaves) if l[0] == 0)
            prototype = list(struct.unpack_from('<9i',payloads[3]))
            tree = bytearray()
            for i in range(511):
                node = prototype.copy()
                node[1:3] = [child if child < 511 else -1-leaf for child in (i*2+1,i*2+2)]
                tree.extend(struct.pack('<9i',*node))
            mutated.write_bytes(replace_lump(original_bsp,3,tree))
            _, repeated_leaf = analyze(mutated,prt,directory.name+'-repeated-leaf',base)
            assert repeated_leaf['cluster_costs'][0]['reachable_leaf_records'] == 1
            assert repeated_leaf['unmapped_clusters'] == int(lines[1])-1
            assert not repeated_leaf['stored_pvs_costs_available']

            # Duplicate one opening and retain hint, sky and unknown flag bits.
            duplicate = lines[4].split(); duplicate[3] = '7'
            modified = lines[:]; modified[2] = str(count+1); modified.insert(4,' '.join(duplicate))
            altered = directory/'parallel.prt'; altered.write_text('\n'.join(modified)+'\n')
            _, repeated = analyze(path,altered,directory.name+'-parallel',base)
            assert any(r['extra_parallel_openings'] for r in repeated['regions'])
            assert repeated['openings'][0]['flags'] == 7 and not repeated['openings'][0]['graph_bridge']
            # Force independent bridge/component controls, including parallel edges
            # and isolated clusters. These deliberately mismatched carriers are
            # graph tests, not geometric evidence of correspondence.
            points = original.splitlines()[4].split('(',1)[1]
            prefix = original.splitlines()[4].split()[0]
            text = '\n'.join(['PRT1',lines[1],'4','0',
                 f'{prefix} 0 1 0 ({points}',f'{prefix} 0 1 0 ({points}',
                 f'{prefix} 1 2 1 ({points}',f'{prefix} 2 3 2 ({points}'])+'\n'
            altered.write_text(text)
            _, bridge = analyze(path,altered,directory.name+'-bridges',base)
            assert bridge['bridge_portals'] == 2 and bridge['components'] == int(lines[1])-3
            assert bridge['pair_probes']['disagree'] > 0
            # Equal dimensions do not establish geometric correspondence.
            def translated(match):
                xyz = list(map(float,match[1].split())); xyz[0] += 4096
                return '('+' '.join(format(v,'.9g') for v in xyz)+')'
            altered.write_text(re.sub(r'\(([^)]+)\)',translated,original))
            _, shifted = analyze(path,altered,directory.name+'-shifted-portals',base)
            assert shifted['pair_probes']['disagree'] == count
            # Duplicate full contours and nonplanar quads are not usable windings.
            modified = lines.copy(); tokens = lines[4][:lines[4].index('(')].split()
            points = re.findall(r'\(([^)]+)\)',lines[4]); tokens[0] = str(len(points)*2)
            modified[4] = ' '.join(tokens)+' '+' '.join('('+p+')' for p in points*2)
            altered.write_text('\n'.join(modified)+'\n')
            _, twice = analyze(path,altered,directory.name+'-repeated-contour',base)
            assert twice['pair_probes']['unusable_windings'] == 1
            assert twice['openings'][0]['area'] is None
            modified = lines.copy()
            for index in range(4,4+count):
                points = [list(map(float,p.split())) for p in re.findall(r'\(([^)]+)\)',lines[index])]
                if len(points)==4:
                    axis = min(range(3),key=lambda a:max(p[a] for p in points)-min(p[a] for p in points))
                    points[0][axis] += 1
                    modified[index] = lines[index][:lines[index].index('(')]+' '.join('(%g %g %g)'%tuple(p) for p in points)
                    break
            else:
                raise AssertionError('Expected a quadrilateral control')
            altered.write_text('\n'.join(modified)+'\n')
            _, bent = analyze(path,altered,directory.name+'-nonplanar',base)
            assert bent['pair_probes']['unusable_windings'] == 1
            modified = lines.copy(); modified[1] = str(int(lines[1])+1)
            altered.write_text('\n'.join(modified)+'\n')
            _, missing = analyze(path,altered,directory.name+'-extra-cluster',base)
            assert missing['unmapped_clusters'] == 1 and not missing['stored_pvs_costs_available']
            for label, bad in [('trailing',original+'junk'),('parenthesis',original.replace(')',']',1)),
                               ('coordinate',original.replace('(', '(nan ',1))]:
                altered.write_text(bad); failure(path,altered,directory.name+'-'+label,base)
            protected = directory/'input.json'; protected.write_text(original)
            failure(path,protected,directory.name+'-protect-prt',base,output=protected)

    (root/'validation.json').write_text(json.dumps({'schema_version':1,
        'compiler_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'known_source_pairs':records,
        'checks':['graph edge-removal bridge oracle','regional membership and degree work oracle','fan-area/perimeter/shape oracle',
                  'unique world surface/triangle PVS oracle','missing VIS','1/4-worker exact reports',
                  'combined brush-cell analysis','exact and exhausted work limits','hint/sky/unknown flags',
                  'parallel openings and isolated clusters','mismatched probe disagreement',
                  'shared nodes and 512 references to one leaf','PVS padding and row-dimension mismatch',
                  'nonplanar quads and repeated contours',
                  'strict syntax failures and input/output preservation'],'result':'passed'},indent=2)+'\n')
    print('Regional PRT graph, independent graph/PVS oracles, paired detail controls and protected failure checks passed')


if __name__ == '__main__':
    main()

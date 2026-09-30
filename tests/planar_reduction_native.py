"""Read-only reduction measurements on original compiled IBSP fixtures.

This checks geometric/attribute candidates, not runtime material eligibility.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from fractions import Fraction as F
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from fixtures import create_fixture
from integration import Bsp, run
from planar_reduction import bits, cross, mesh_boundary, sample


def fixture(root, lighting):
    source = create_fixture(root, patch=False)
    game = source.parent.parent
    models = game/'models/q3mapx'
    models.mkdir(parents=True, exist_ok=True)
    # The model importer rotates Y-up to Z-up. The generated OBJ has a flat
    # horizontal face at Quake z=128, with exact dyadic UVs and no shared assets.
    n = 16
    lines = ['mtllib grid.mtl', 'usemtl textures/q3mapx/grid']
    for y in range(n+1):
        for x in range(n+1):
            lines.append(f'v {x*16-128} 0 {128-y*16}')
            lines.append(f'vt {x/n} {y/n}')
    lines.append('vn 0 1 0')
    for y in range(n):
        for x in range(n):
            a = y*(n+1)+x+1
            b, c, d = a+1, a+n+2, a+n+1
            # Assimp reverses winding on import; supply the Y-up OBJ winding
            # matching vn so the resulting Quake face is visible from above.
            for tri in ((a, b, d), (b, c, d)):
                lines.append('f '+' '.join(f'{i}/{i}/1' for i in tri))
    (models/'grid.obj').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    (models/'grid.mtl').write_text('newmtl textures/q3mapx/grid\nKd 1 1 1\n', encoding='utf-8')
    shader = game/'scripts/q3mapx_tests.shader'
    shader.write_text(shader.read_text(encoding='utf-8')+'''
textures/q3mapx/grid
{
    qer_editorimage textures/q3mapx/checker.tga
    { map $lightmap }
    { map textures/q3mapx/checker.tga blendFunc filter }
}
''', encoding='utf-8')
    text = source.read_text(encoding='utf-8')
    if lighting == 'ambient':
        text = text.replace('"classname" "worldspawn"', '"classname" "worldspawn"\n"_ambient" "64"')
        text = text.replace('{\n"classname" "light"\n"origin" "0 0 224"\n"light" "450"\n}\n', '')
    text += '{\n"classname" "misc_model"\n"origin" "0 0 128"\n"model" "models/q3mapx/grid.obj"\n"spawnflags" "4"\n}\n'
    source.write_text(text, encoding='utf-8')
    return source


def meshes(bsp):
    vertex_data, index_data, surface_data = bsp.lump(10), bsp.lump(11), bsp.lump(13)
    shaders = [bsp.lump(1)[i:i+64].split(b'\0')[0].decode() for i in range(0, len(bsp.lump(1)), 72)]
    for offset in range(0, len(surface_data), 104):
        shader, fog, kind, first_v, count_v, first_i, count_i = struct.unpack_from('<7i', surface_data, offset)
        if kind not in (1, 3) or not count_i:
            continue
        assert count_i % 3 == 0 and first_v >= 0 and first_i >= 0
        assert (first_v+count_v)*44 <= len(vertex_data) and (first_i+count_i)*4 <= len(index_data)
        vertices = []
        for i in range(first_v, first_v+count_v):
            xyz_st_lm_normal = struct.unpack_from('<10f', vertex_data, i*44)
            xyz, st, lm, normal = (xyz_st_lm_normal[:3], xyz_st_lm_normal[3:5],
                                  xyz_st_lm_normal[5:7], xyz_st_lm_normal[7:])
            color = struct.unpack_from('<4B', vertex_data, i*44+40)
            vertices.append((*xyz, *normal, *st, *lm, *([0.0]*6), *map(float, color), *([0.0]*12)))
        indices = struct.unpack_from(f'<{count_i}i', index_data, first_i*4)
        assert all(0 <= i < count_v for i in indices)
        triangles = [tuple(indices[i:i+3]) for i in range(0, count_i, 3)]
        yield {'surface': offset//104, 'shader': shaders[shader], 'fog': fog, 'type': kind,
               'vertices': vertices, 'triangles': triangles}


def analyze(unit, bsp_path, root, label):
    original = bsp_path.read_bytes()
    surfaces = list(meshes(Bsp(bsp_path)))
    payload = []
    for surface in surfaces:
        vertices, triangles = surface['vertices'], surface['triangles']
        payload.append(f'{len(vertices)} {len(triangles)} 20000000')
        payload.extend(' '.join(format(bits(v), 'x') for v in p) for p in vertices)
        payload.extend(' '.join(map(str, tri)) for tri in triangles)
    data = ('\n'.join(payload)+'\n').encode()
    result = subprocess.run([str(unit), '--mesh'], input=data, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, cwd=root, timeout=90)
    (root/f'{label}-reduction.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode == 0, result.stderr.decode(errors='replace')
    rows = iter(result.stdout.decode().splitlines())
    records, samples = [], 0
    for surface in surfaces:
        count, edits, work, topology, planar, attributes = map(int, next(rows).split())
        output = [tuple(map(int, next(rows).split())) for _ in range(count)]
        vertices, triangles = surface['vertices'], surface['triangles']
        assert all(len(t) == 3 and all(0 <= i < len(vertices) for i in t) for t in output)
        assert count+2*edits == len(triangles)
        assert mesh_boundary(output) == mesh_boundary(triangles)
        # Native grid coordinates project into XY. Check both directions to
        # detect removed coverage as well as newly introduced coverage.
        if surface['shader'] == 'textures/q3mapx/grid':
            points = [tuple(map(F, v)) for v in vertices]
            area = lambda tris: sum(cross(*(points[i] for i in t)) for t in tris)
            assert area(triangles) == area(output) != 0
            for domain in (triangles, output):
                stride = max(1, len(domain)//5)
                for tri in domain[::stride]:
                    point = tuple(sum(points[i][f]*weight for i, weight in zip(tri, (F(1,7), F(2,7), F(4,7)))) for f in range(2))
                    assert sample(points, triangles, point) == sample(points, output, point)
                    samples += 1
        records.append({k: surface[k] for k in ('surface', 'shader', 'fog', 'type')} | {
            'input_vertices': len(vertices), 'input_triangles': len(triangles),
            'output_triangles': count, 'removed_interior_vertices': edits, 'work_units': work,
            'topology_rejections': topology, 'planar_rejections': planar,
            'attribute_rejections': attributes,
            'output_indices_sha256': hashlib.sha256(json.dumps(output).encode()).hexdigest()})
    assert next(rows, None) is None
    assert bsp_path.read_bytes() == original, 'Read-only reduction changed the BSP'
    grid = [r for r in records if r['shader'] == 'textures/q3mapx/grid']
    assert grid and sum(r['input_triangles'] for r in grid) == 512, grid
    return {'stage': label, 'bsp_sha256': hashlib.sha256(original).hexdigest(),
            'input_sha256': hashlib.sha256(data).hexdigest(), 'samples': samples, 'surfaces': records,
            'grid_input_triangles': sum(r['input_triangles'] for r in grid),
            'grid_output_triangles': sum(r['output_triangles'] for r in grid)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--unit', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    args = parser.parse_args()
    root, compiler, unit = args.work_dir.resolve(), args.compiler.resolve(), args.unit.resolve()
    root.mkdir(parents=True, exist_ok=True)
    records = []
    for lighting in ('ambient', 'point'):
        directory = root/lighting
        source = fixture(directory, lighting)
        base = ['-game', 'quake3', '-threads', '1', '-fs_basepath', directory, '-fs_homepath', directory/'home']
        run(compiler, [*base, '-meta', source], directory, 'bsp')
        bsp = source.with_suffix('.bsp')
        before = analyze(unit, bsp, directory, 'before-light')
        run(compiler, [*base, '-vis', source], directory, 'vis')
        run(compiler, [*base, '-light', '-fast', source], directory, 'light')
        after = analyze(unit, bsp, directory, 'after-light')
        assert before['grid_output_triangles'] < before['grid_input_triangles']
        if lighting == 'ambient':
            assert after['grid_output_triangles'] < after['grid_input_triangles']
        else:
            assert after['grid_output_triangles'] > before['grid_output_triangles'], 'Point-light variation must protect vertices'
        records.append({'lighting': lighting, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                        'before_light': before, 'after_light': after})
    report = {'scope': 'Read-only mathematical candidates; no material/renderer approval or BSP rewriting',
              'compiler_sha256': hashlib.sha256(compiler.read_bytes()).hexdigest(),
              'unit_sha256': hashlib.sha256(unit.read_bytes()).hexdigest(), 'records': records}
    (root/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('; '.join(f"{r['lighting']}: 512 -> {r['before_light']['grid_output_triangles']} before LIGHT, "
                    f"{r['after_light']['grid_output_triangles']} after LIGHT" for r in records))


if __name__ == '__main__':
    main()

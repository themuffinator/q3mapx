"""Native multi-triangle texture recovery, seam controls and MAP rebuilds.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess

from fixtures import create_fixture
from integration import run
from patch_input import payloads


def reference_uv_policy(compiler, directory):
    """References before consensus have no policy flag; newer ones require it."""
    directory.mkdir(parents=True, exist_ok=True)
    result = subprocess.run([str(compiler), '-help', '-convert'], cwd=directory, capture_output=True, timeout=20)
    (directory/'reference-help.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode == 0
    return ['-uv-policy','triangle'] if b'-uv-policy' in result.stdout else []


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def pack(data, parts):
    result = bytearray(data[:8+8*len(parts)])
    for i, part in enumerate(parts):
        struct.pack_into('<2i', result, 8+8*i, len(result), len(part))
        result.extend(part); result.extend(bytes(-len(result) % 4))
    return bytes(result)


def sizes(data):
    return (80, 148, 52) if data[:4] == b'RBSP' else (44, 104, 28)


def model_origin(parts, model):
    if model == 0: return [0, 0, 0]
    for text in re.findall(rb'\{([^}]*)\}', parts[0]):
        if re.search(rb'"model"\s+"\*'+str(model).encode()+rb'"', text):
            origin = re.search(rb'"origin"\s+"([^"]+)"', text)
            return list(map(float, origin[1].split())) if origin else [0,0,0]
    raise AssertionError('Missing model origin')


def top_surface(data, model):
    parts = payloads(data); stride, surface_stride, normal_offset = sizes(data)
    first, count = struct.unpack_from('<2i', parts[7], model*40+24)
    candidates = []
    for index in range(first, first+count):
        kind, v, n = struct.unpack_from('<3i', parts[13], index*surface_stride+8)
        if kind != 1: continue
        normal = struct.unpack_from('<3f', parts[10], v*stride+normal_offset)
        positions = [struct.unpack_from('<3f', parts[10], (v+j)*stride) for j in range(n)]
        if normal[2] > .99 and (model != 0 or abs(positions[0][2]-96) < .01):
            candidates.append((index, v, positions))
    assert candidates
    return max(candidates, key=lambda item: item[2][0][2])


def truth(x, y):
    angle = math.radians(27)
    return [math.cos(angle)/(.7*64)*x - math.sin(angle)/(.7*64)*y + 63.12345,
            -math.sin(angle)/(1.3*64)*x - math.cos(angle)/(1.3*64)*y - 31.8765]


def render_islands(data, model, mode):
    """Simulate small visible pieces of an otherwise large editable brush face.

    UVs are rounded to native binary32, as on compiler-generated BSP vertices.
    The analytic authoring field is used only to generate/evaluate the fixture.
    """
    parts = list(payloads(data)); stride, surface_stride, normal_offset = sizes(data)
    target, old_first, old_positions = top_surface(data, model)
    origin = model_origin(parts, model)
    xs, ys = [[p[axis] for p in old_positions] for axis in (0, 1)]
    cx, cy = (min(xs)+max(xs))/2, (min(ys)+max(ys))/2
    ex, ey = (max(xs)-min(xs))*.4, (max(ys)-min(ys))*.4
    z = old_positions[0][2]
    triangles = []
    for iy in range(-2, 3):
        for ix in range(-2, 3):
            x, y = cx+ex*ix/2, cy+ey*iy/2
            radius = .2 if (ix, iy) == (-2, -2) else .0625
            points = [(x-radius,y-radius), (x-radius,y+radius), (x+radius,y+radius), (x+radius,y-radius)]
            for ids in ((0,1,2), (0,2,3)):
                triangle = []
                for i in ids:
                    px, py = map(f32, points[i]); uv = truth(px+origin[0], py+origin[1])
                    if mode == 'seam' and ix > 0: uv[0] += .25
                    triangle.append((px, py, z, *uv))
                triangles.append(triangle)
    if mode == 'limit': triangles = (triangles*83)[:4097]
    if mode == 'reordered':
        triangles.reverse(); triangles = [tri[1:]+tri[:1] for tri in triangles]
    if mode == 'nearby':
        # An unrelated, larger parallel render triangle must not contaminate the
        # strict joint fit, even though it meets the legacy two-unit search bound.
        triangles.append([(cx-ex,cy-ey,z+1.5,1,1), (cx-ex,cy+ey,z+1.5,1,2), (cx+ex,cy+ey,z+1.5,2,2)])
    template = parts[10][old_first*stride:(old_first+1)*stride]
    vertices = bytearray(parts[10]); indices = bytearray(parts[11]); surfaces = bytearray(parts[13])
    first_v, first_i = len(vertices)//stride, len(indices)//4
    for triangle in triangles:
        for vertex in triangle:
            value = bytearray(template)
            struct.pack_into('<5f', value, 0, *vertex)
            vertices.extend(value)
    indices.extend(struct.pack('<'+'i'*(len(triangles)*3), *range(len(triangles)*3)))
    struct.pack_into('<4i', surfaces, target*surface_stride+12, first_v, len(triangles)*3, first_i, len(triangles)*3)
    parts[10], parts[11], parts[13] = vertices, indices, surfaces
    return pack(data, parts), target


def solid_signature(data):
    parts = payloads(data); side_stride = 12 if data[:4] == b'RBSP' else 8
    brushes = []
    for i in range(0, len(parts[8]), 12):
        first, count, shader = struct.unpack_from('<3i', parts[8], i)
        sides = []
        for j in range(first, first+count):
            plane, material = struct.unpack_from('<2i', parts[9], j*side_stride)
            sides.append((struct.unpack_from('<4f',parts[2],plane*16), parts[1][material*72:(material+1)*72].hex()))
        brushes.append((parts[1][shader*72:(shader+1)*72].hex(), sorted(sides)))
    return [sorted(brushes[first:first+count]) for first, count in
            (struct.unpack_from('<2i',parts[7],i+32) for i in range(0,len(parts[7]),40))]


def rebuilt_error(data, model):
    parts = payloads(data); stride, _, _ = sizes(data)
    _, first, positions = top_surface(data, model)
    origin = model_origin(parts, model)
    observed = [struct.unpack_from('<2f',parts[10],(first+i)*stride+12) for i in range(len(positions))]
    wanted = [truth(p[0]+origin[0],p[1]+origin[1]) for p in positions]
    # One uniform integer bias per channel accommodates the established MAP
    # writer's tile wrapping. Per-vertex modular matching could conceal slopes.
    bias = [round(observed[0][axis]-wanted[0][axis]) for axis in range(2)]
    errors = [abs(a[axis]-b[axis]-bias[axis]) for a,b in zip(observed,wanted) for axis in range(2)]
    return max(errors)


def target_mapping(data, model):
    parts = payloads(data); stride, _, _ = sizes(data)
    _, first, positions = top_surface(data, model)
    return {point: struct.unpack_from('<2f', parts[10], (first+i)*stride+12) for i,point in enumerate(positions)}


def same_fallback_mapping(a, b, model):
    # The compatibility writer wraps integer shifts and uses fewer decimal
    # digits. Compare the rebuilt selected field, not its textual spelling;
    # one uniform bias is allowed, never independent per-vertex wrapping.
    left, right = target_mapping(a,model), target_mapping(b,model)
    assert left.keys() == right.keys()
    first = next(iter(left))
    bias = [round(left[first][axis]-right[first][axis]) for axis in range(2)]
    assert max(abs(left[p][axis]-right[p][axis]-bias[axis]) for p in left for axis in range(2)) < 1e-5


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--reference', type=Path)
    p.add_argument('--work-dir', type=Path, required=True)
    a = p.parse_args(); compiler = a.compiler.resolve(); root = a.work_dir.resolve()
    reference = a.reference.resolve() if a.reference else None
    reference_options = reference_uv_policy(reference, root) if reference else []
    results = []; ordered_maps = {}
    for game in ('quake3', 'ja'):
        directory = root/game
        shader_directory = 'shaders' if game == 'ja' else 'scripts'
        source = create_fixture(directory, patch=False, shader_directory=shader_directory)
        if game == 'ja':
            shader = directory/'baseq3/shaders/q3mapx_tests.shader'
            shader.write_text(shader.read_text()+'\ntextures/system/origin\n{\n surfaceparm origin\n surfaceparm nodraw\n}\n')
        base = ['-game', game, '-fs_basegame', 'baseq3', '-fs_basepath', directory,
                '-fs_homepath', directory/'home', '-threads', 1]
        run(compiler,[*base,'-meta',source],directory,'prepare')
        original = source.with_suffix('.bsp').read_bytes()
        geometry = solid_signature(original)
        for model in (0,1):
            if model: assert model_origin(payloads(original), model) == [112,0,64]
            for mode in ('quantized','reordered','seam','nearby','limit'):
                native, target = render_islands(original, model, mode)
                path = directory/f'{model}-{mode}.bsp'; path.write_bytes(native)
                for fmt in ('map','map_bp','map_220'):
                    name = f'{model}-{mode}-{fmt}'
                    outputs, errors, summaries, rebuilt_outputs = {}, {}, {}, {}
                    for policy in ('triangle','consensus'):
                        output = directory/f'{name}-{policy}.map'
                        run(compiler,[*base,'-decompile','-format',fmt,'-uv-policy',policy,'-o',output,path],directory,name+'-'+policy)
                        outputs[policy] = output.read_bytes()
                        report = json.loads(Path(str(output)+'.recovery.json').read_text())
                        summaries[policy] = report['uv_recovery']
                        assert report['uv_recovery']['policy'] == policy
                        run(compiler,[*base,'-meta',output],directory,name+'-'+policy+'-rebuild')
                        rebuilt = output.with_suffix('.bsp').read_bytes()
                        rebuilt_outputs[policy] = rebuilt
                        assert solid_signature(rebuilt) == geometry, (name, 'brush geometry/material/contents changed')
                        if mode in ('quantized','reordered','nearby'): errors[policy] = rebuilt_error(rebuilt,model)
                    if reference:
                        old = directory/f'{name}-reference.map'
                        run(reference,[*base,'-decompile','-format',fmt,*reference_options,'-o',old,path],directory,name+'-reference')
                        assert old.read_bytes() == outputs['triangle'], (name, 'triangle policy changed preceding MAP')
                    records = [record for record in summaries['consensus']['faces'] if target in record['surfaces']]
                    if mode == 'limit':
                        assert summaries['consensus']['counts'].get('sample_limit',0) > 0
                        assert any(record['triangles'] > 4096 and record['rms_error'] is None for record in summaries['consensus']['faces'])
                    else:
                        assert records, (name, summaries['consensus'])
                        expected = ('conflicting_mappings',) if mode == 'seam' else ('consistent','triangle_consistent')
                        assert any(record['status'] in expected for record in records), (name, records)
                    if mode in ('seam','limit'):
                        same_fallback_mapping(rebuilt_outputs['consensus'], rebuilt_outputs['triangle'], model)
                    if mode == 'quantized': ordered_maps[game,model,fmt] = outputs['consensus']
                    if mode == 'reordered': assert outputs['consensus'] == ordered_maps[game,model,fmt], (name,'triangle-order dependence')
                    if mode == 'quantized' and fmt == 'map_220':
                        parallel = directory/f'{name}-workers4.map'
                        run(compiler,[*base[:-1],4,'-decompile','-o',parallel,path],directory,name+'-workers4')
                        assert parallel.read_bytes() == outputs['consensus']
                        assert json.loads(Path(str(parallel)+'.recovery.json').read_text())['uv_recovery'] == summaries['consensus']
                    if errors:
                        assert errors['consensus'] < 0.0001, (name, errors)
                        assert errors['consensus'] < errors['triangle']*.25, (name, errors)
                    assert path.read_bytes() == native
                    results.append({'game':game,'model':model,'case':mode,'format':fmt,
                                    'rebuilt_max_uv_error':errors,'brush_geometry_preserved':True,
                                    'triangle_reference_map_parity':bool(reference),
                                    'uv_counts':summaries['consensus']['counts']})
        # Invalid requests fail before publication. The default also works in
        # the workbench's ordinary decompile command without a new required flag.
        output = directory/'option-check.map'; output.write_bytes(b'previous map')
        report = Path(str(output)+'.recovery.json'); report.write_bytes(b'previous report')
        for opts in (['-uv-policy','bad'], ['-uv-policy','consensus','-fast'], ['-uv-policy','triangle','-format','obj']):
            result = subprocess.run([str(compiler), *map(str,[*base,'-decompile',*opts,'-o',output,source.with_suffix('.bsp')])],
                                    cwd=directory,capture_output=True,timeout=20)
            assert result.returncode == 1 and b'ERROR' in result.stdout+result.stderr
            assert output.read_bytes() == b'previous map' and report.read_bytes() == b'previous report'
        run(compiler,[*base,'-decompile','-o',output,source.with_suffix('.bsp')],directory,'default')
        assert json.loads(report.read_text())['uv_recovery']['policy'] == 'consensus'
    (root/'validation.json').write_text(json.dumps({'compiler_sha256':hashlib.sha256(compiler.read_bytes()).hexdigest(),
        'reference_sha256':hashlib.sha256(reference.read_bytes()).hexdigest() if reference else None,
        'cases':results},indent=2)+'\n')
    print(f'{len(results)} native UV recovery/rebuild cases passed across IBSP/RBSP, world/entity and three MAP formats')


if __name__ == '__main__': main()

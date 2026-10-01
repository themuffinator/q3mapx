"""Authored patch RGBA: interpolation, topology, LIGHT, relighting and failure safety.
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
from patch_input import patch, payloads
from surface_density import extras


def color_at(x, y):
    return ((10, 250, 40)[x], (20, 200, 80)[y], 255 if x == y == 1 else 0,
            0 if x == y == 1 else 255)


def painted(*, mode='material', subdivisions=8, size=8, curved=False,
            origin=(-224, -224, 128), width=3, height=3, constant=None, shader='paint', extent=128):
    rows = []
    for x in range(width):
        row = []
        for y in range(height):
            rgba = constant or color_at(x % 3, y % 3)
            if mode == 'lighting': rgba = (255, 255, 255, rgba[3])
            xyz = (origin[0] + x * extent/2, origin[1] + y * extent/2,
                   origin[2] + (48 if curved and x % 2 else 0) + (32 if curved and y % 2 else 0))
            row.append('( ' + ' '.join(map(str, (*xyz, x, y, *rgba))) + ' )')
        rows.append('( ' + ' '.join(row) + ' )')
    return ('{\nq3mapxPatchDef2\n{\nq3mapx/' + shader + f'\n( {width} {height} 0 0 0 )\n'
            f'lightmapSampleSize {size}\nvertexRGB {mode}\npaintSubdivisions {subdivisions}\n(\n'
            + '\n'.join(rows) + '\n)\n}\n}\n')


def surfaces(lumps):
    raven = len(lumps) == 18
    stride, vertex_stride, color_offset = (148, 80, 64) if raven else (104, 44, 40)
    for index, offset in enumerate(range(0, len(lumps[13]), stride)):
        shader, fog, kind, first, count, first_index, index_count = struct.unpack_from('<7i', lumps[13], offset)
        verts = []
        for v in range(first, first + count):
            location = v * vertex_stride
            xyz = struct.unpack_from('<3f', lumps[10], location)
            rgba = tuple(lumps[10][location + color_offset:location + color_offset + (16 if raven else 4)])
            verts.append((xyz, rgba))
        yield index, kind, verts, shader, index_count


def paint_rows(lumps, srf):
    return [(i, kind, verts, shader, indices) for i, kind, verts, shader, indices in surfaces(lumps)
            if srf[i].get('patchPaintMode')]


def expected(xyz, mode, *, modifier=False):
    u, v = (xyz[0] + 224) / 128, (xyz[1] + 224) / 128
    bu, bv = ((1-u)**2, 2*u*(1-u), u*u), ((1-v)**2, 2*v*(1-v), v*v)
    rgba = [math.floor(sum(bu[x]*bv[y]*color_at(x, y)[k] for x in range(3) for y in range(3)) + .5 + 1e-7)
            for k in range(4)]
    if mode == 'lighting': rgba[:3] = [255]*3
    if modifier:
        rgba[0] = math.floor(rgba[0]*.5)
        rgba[2] = math.floor(rgba[2]*.25)
        rgba[3] = math.floor(rgba[3]*.5)
    return tuple(rgba)


def normalized(lumps):
    return [re.sub(rb'"_q3map2_cmdline" "[^"\n]*"', b'', lumps[0]), *lumps[1:]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--work-dir', required=True, type=Path)
    args = parser.parse_args()
    compiler, root = args.compiler.resolve(), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    directory = root / 'native'
    source = create_fixture(directory, patch=False)
    plain = source.read_text()
    marker = '"message" "q3mapx regression"\n'
    scripts = source.parent.parent / 'scripts/q3mapx_tests.shader'
    scripts.write_text(scripts.read_text() + '''
textures/q3mapx/paint
{
    qer_editorimage textures/q3mapx/checker.tga
    { map $whiteimage rgbGen vertex alphaGen vertex }
    { map $lightmap blendFunc filter }
}
textures/q3mapx/paint_unlit
{
    surfaceparm nolightmap
    { map $whiteimage rgbGen vertex alphaGen vertex }
}
textures/q3mapx/paint_mod
{
    q3map_colorMod scale ( 0.5 1 0.25 )
    q3map_alphaMod scale 0.5
    { map $whiteimage rgbGen vertex alphaGen vertex }
    { map $lightmap blendFunc filter }
}
textures/q3mapx/paint_sprite
{
    deformVertexes autosprite
    { map $whiteimage }
}
textures/q3mapx/paint_indexed
{
    q3map_indexed
    { map $whiteimage }
}
''')
    # Raven profiles read shaders/; Quake III reads scripts/. Exercise the
    # actual material definitions on both, rather than fallback materials.
    raven_scripts = source.parent.parent/'shaders'
    raven_scripts.mkdir(exist_ok=True)
    (raven_scripts/'shaderlist.txt').write_text('q3mapx_tests\n')
    (raven_scripts/'q3mapx_tests.shader').write_bytes(scripts.read_bytes())
    base = ['-fs_basepath', directory, '-fs_homepath', directory/'home', '-fs_basegame', 'baseq3']

    def compile_case(label, text, *, game='quake3', threads=1, options=()):
        source.write_text(plain.replace(marker, marker + text))
        argv = ['-game', game, *base, '-threads', threads, *options, source]
        run(compiler, argv, directory, label)
        return payloads(source.with_suffix('.bsp').read_bytes()), extras(source), argv

    legacy, _, legacy_argv = compile_case('legacy', patch(), options=['-meta', '-patchmeta'])
    legacy_srf = source.with_suffix('.srf').read_bytes()
    if args.reference:
        run(args.reference.resolve(), legacy_argv, directory, 'legacy-reference')
        assert payloads(source.with_suffix('.bsp').read_bytes()) == legacy
        assert source.with_suffix('.srf').read_bytes() == legacy_srf
        legacy_bsp = source.with_suffix('.bsp').read_bytes()
        lighting_args = ['-game', 'quake3', *base, '-threads', 1, '-light', '-fast', source]
        run(compiler, lighting_args, directory, 'legacy-new-light')
        baked = payloads(source.with_suffix('.bsp').read_bytes())
        source.with_suffix('.bsp').write_bytes(legacy_bsp)
        run(args.reference.resolve(), lighting_args, directory, 'legacy-reference-light')
        assert payloads(source.with_suffix('.bsp').read_bytes()) == baked, 'legacy LIGHT output changed'

    records = []
    for game in ('quake3', 'ja'):
        for mode in ('material', 'lighting'):
            for curved in (False, True):
                previous = None
                previous_bake = None
                for threads in (1, 4):
                    label = f'{game}-{mode}-' + ('curve' if curved else 'flat') + f'-t{threads}'
                    lumps, srf, _ = compile_case(label, painted(mode=mode, curved=curved), game=game, threads=threads)
                    assert 'q3mapx_tests.shader' in (directory/(label+'.log')).read_text()
                    assert b'"_q3mapx_patchPaint1"' in lumps[0]
                    rows = paint_rows(lumps, srf)
                    assert rows and {srf[i]['patchPaintMode'] for i, *_ in rows} == {2 if mode == 'material' else 1}
                    render = [r for r in rows if r[1] != 2]
                    collision = [r for r in rows if r[1] == 2]
                    assert render and collision
                    assert sum(r[4]//3 for r in render) == 128, (label, [(r[1], r[4]) for r in rows])
                    for _, kind, verts, shader, _ in rows:
                        flags = struct.unpack_from('<i', lumps[1], shader*72+64)[0]
                        assert bool(flags & (2097152 if game == 'ja' else 128)) == (kind == 2), (label, kind, flags)
                    for i, _, verts, _, _ in render:
                        assert srf[i]['sampleSize'] == srf[i]['authoredSampleSize'] == 8
                        first_vertex = struct.unpack_from('<i', lumps[13], i*(148 if game == 'ja' else 104)+12)[0]
                        for local, (xyz, color) in enumerate(verts):
                            u, v = (xyz[0]+224)/128, (xyz[1]+224)/128
                            height = 128 + (96*u*(1-u)+64*v*(1-v) if curved else 0)
                            assert abs(xyz[2]-height) < 1e-5, (label, xyz, height)
                            offset = (first_vertex+local)*(80 if game == 'ja' else 44)
                            st = struct.unpack_from('<2f', lumps[10], offset+12)
                            normal = struct.unpack_from('<3f', lumps[10], offset+(52 if game == 'ja' else 28))
                            assert abs(sum(n*n for n in normal)-1) < 1e-5
                            # Meta assembly may bias a whole chart by integer texture repeats.
                            assert all(abs((a-b+.5) % 1-.5) < 1e-5 for a,b in zip(st,(2*u,2*v)))
                            want = expected(xyz, mode)
                            assert color[:4] == want, (label, xyz, color, want)
                            if game == 'ja': assert color == want*4
                    if previous is not None: assert normalized(lumps) == previous, label
                    previous = normalized(lumps)
                    before = {i: verts for i, _, verts, _, _ in rows}
                    args_base = ['-game', game, *base, '-threads', threads]
                    run(compiler, [*args_base, '-vis', source], directory, label+'-vis')
                    # Both direct and bounced saves must retain material data.
                    options = ['-fast', '-bounce', 1] if curved else ['-fast', '-approx', 64]
                    repeated_bake = None
                    for attempt in range(2):
                        run(compiler, [*args_base, '-light', *options, source], directory, label+f'-light-{attempt}', timeout=180)
                        baked = payloads(source.with_suffix('.bsp').read_bytes())
                        if repeated_bake is not None:
                            assert normalized(baked) == repeated_bake, (label, 'relight divergence')
                        repeated_bake = normalized(baked)
                        for i, kind, verts, _, _ in paint_rows(baked, srf):
                            assert len(verts) == len(before[i])
                            for (xyz, color), (old_xyz, old_color) in zip(verts, before[i]):
                                assert xyz == old_xyz
                                assert color[3::4] == old_color[3::4], (label, 'alpha', xyz)
                                if mode == 'material': assert color == old_color, (label, 'RGB', xyz, color, old_color)
                        if mode == 'lighting':
                            assert any(color[:3] != (255,)*3 for i, kind, verts, _, _ in paint_rows(baked, srf)
                                       if kind != 2 for xyz, color in verts), 'alpha-only paint must still receive vertex light'
                    if previous_bake is not None:
                        assert repeated_bake == previous_bake, (label, 'LIGHT worker divergence')
                    previous_bake = repeated_bake
                    records.append({'case': label, 'render_triangles': sum(r[4]//3 for r in render),
                                    'render_surfaces': len(render), 'relights': 2,
                                    'repeated_light_lump_parity': True,
                                    'bsp_sha256': hashlib.sha256(source.with_suffix('.bsp').read_bytes()).hexdigest()})

    # Different RGB on an identical shared boundary must survive both vertex
    # deduplication passes, including normal smoothing and final meta merging.
    for mode in ('material', 'lighting'):
        text = painted(origin=(-192, -192, 128), constant=(255, 0, 0, 255))
        text += painted(origin=(-64, -192, 128), mode=mode, constant=(0, 0, 255, 255))
        lumps, srf, _ = compile_case('shared-'+mode, text, options=['-meta', '-np', 60])
        colors = {rgba[:4] for i, kind, verts, _, _ in paint_rows(lumps, srf) if kind != 2
                  for xyz, rgba in verts if xyz[0] == -64 and xyz[1] == -128}
        assert colors == {(255, 0, 0, 255), (0, 0, 255, 255) if mode == 'material' else (255, 255, 255, 255)}, colors
        if mode == 'lighting': assert {r['patchPaintMode'] for r in srf.values() if r.get('patchPaintMode')} == {1, 2}

    # Painting must not make a valid small patch disappear merely because
    # refinement produces edges below the legacy meta triangle cutoff.
    tiny, tiny_srf, _ = compile_case('tiny-painted', painted(extent=.5))
    assert sum(row[4]//3 for row in paint_rows(tiny, tiny_srf) if row[1] != 2) == 128

    for shader in ('paint_unlit', 'paint_mod'):
        lumps, srf, _ = compile_case(shader, painted(shader=shader))
        rows = paint_rows(lumps, srf)
        for i, kind, verts, _, _ in rows:
            if kind == 2: continue
            if shader == 'paint_unlit': assert srf[i]['sampleSize'] == 0
            for xyz, color in verts:
                assert color[:4] == expected(xyz, 'material', modifier=shader == 'paint_mod'), (shader, xyz, color)
        run(compiler, ['-game', 'quake3', *base, '-light', '-fast', source], directory, shader+'-light')
        baked = paint_rows(payloads(source.with_suffix('.bsp').read_bytes()), srf)
        assert [r[2] for r in baked] == [r[2] for r in rows], shader

    # Source errors must precede publication/removal of prior BSP stage outputs.
    good = painted()
    compile_case('safety-carrier', good)
    saved = {source.with_suffix(ext): source.with_suffix(ext).read_bytes() for ext in ('.bsp', '.srf', '.prt')}
    saved.update({source.with_suffix(ext): b'previous '+ext.encode() for ext in ('.lin', '.reg')})
    malformed = []
    for value in ('-1', '+1', '256', '1.0', '1e1', 'nan', 'inf', '999999999999', '""', '"1 2"'):
        malformed.append(('rgba-'+str(len(malformed)), good.replace('10 20 0 255', value+' 20 0 255', 1)))
    for value in ('0', '-1', '3', '33', '64', '256', '+8', '8.0', '""'):
        malformed.append(('segments-'+str(len(malformed)), good.replace('paintSubdivisions 8', 'paintSubdivisions '+value)))
    malformed += [('mode', good.replace('vertexRGB material', 'vertexRGB multiply')),
                  ('lighting-rgb', good.replace('vertexRGB material', 'vertexRGB lighting')),
                  ('missing-alpha', good.replace('10 20 0 255', '10 20 0', 1)),
                  ('missing-mode', good.replace('vertexRGB material\n', '')),
                  ('missing-density', good.replace('lightmapSampleSize 8\n', '')),
                  ('extra-color', good.replace('10 20 0 255', '10 20 0 255 0', 1)),
                  ('budget', painted(width=31, height=31, subdivisions=32)),
                  ('autosprite', painted(shader='paint_sprite')), ('indexed', painted(shader='paint_indexed'))]

    def rejects(label, argv, expected_text, protected):
        result = subprocess.run([str(compiler), *map(str, argv)], cwd=directory, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=90)
        (directory/(label+'.log')).write_bytes(result.stdout)
        assert result.returncode == 1, (label, result.returncode, result.stdout[-3000:])
        assert expected_text in result.stdout, (label, result.stdout[-3000:])
        assert all(path.read_bytes() == data for path, data in protected.items()), label

    for label, text in malformed:
        for path, data in saved.items(): path.write_bytes(data)
        source.write_text(plain.replace(marker, marker+text))
        rejects(label, ['-game', 'quake3', *base, source], b'Invalid MAP patch', {source:source.read_bytes(), **saved})

    # SRF modes are authoritative only when bound to this exact BSP. Reject a
    # missing record, mode change, stale geometry/color and malicious indices.
    lumps, srf, _ = compile_case('binding-carrier', good)
    original_bsp = source.with_suffix('.bsp').read_bytes()
    original_srf = source.with_suffix('.srf').read_text()
    original_map = source.read_bytes()
    binding = re.search(rb'"_q3mapx_patchPaint1" "([0-9a-f]{64})"', lumps[0]).group(1)
    variants = [('mode', original_srf.replace('patchPaintMode 2', 'patchPaintMode 1', 1)),
                ('missing-mode', re.sub(r'\s*patchPaintMode 2', '', original_srf, count=1)),
                ('missing-binding', re.sub(r'\s*patchPaintBinding1 \w+', '', original_srf)),
                ('stale-binding', original_srf.replace(binding.decode(), '0'*64)),
                ('duplicate-mode', original_srf.replace('patchPaintMode 2', 'patchPaintMode 2\n\tpatchPaintMode 2', 1)),
                ('trailing-mode', original_srf.replace('patchPaintMode 2', 'patchPaintMode 2 ignored', 1)),
                ('bad-mode', original_srf.replace('patchPaintMode 2', 'patchPaintMode 0', 1)),
                ('default-mode', original_srf.replace('default\n{', 'default\n{\n\tpatchPaintMode 2')),
                ('bad-binding', original_srf.replace(binding.decode(), 'g'*64)),
                ('large-index', original_srf+'\n2147483647\n{\n}\n'),
                ('partial-index', original_srf+'\n1junk\n{\n}\n'),
                ('duplicate-index', original_srf+'\n0\n{\n}\n'),
                ('parent-overflow', original_srf.replace('patchPaintMode 2', 'parent 2147483647\n\tpatchPaintMode 2', 1)),
                ('parent-negative', original_srf.replace('patchPaintMode 2', 'parent -2\n\tpatchPaintMode 2', 1)),
                ('parent-junk', original_srf.replace('patchPaintMode 2', 'parent 0junk\n\tpatchPaintMode 2', 1)),
                ('parent-self', original_srf.replace('patchPaintMode 2', 'parent 0\n\tpatchPaintMode 2', 1)),
                ('incomplete', original_srf.rsplit('}', 1)[0])]
    shader_path = source.parent.parent/'scripts/q3map2_fixture.shader'
    shader_path.write_bytes(b'previous generated shader\n')
    for label, srf_text in variants:
        source.with_suffix('.bsp').write_bytes(original_bsp)
        source.with_suffix('.srf').write_text(srf_text)
        protected = {source:original_map, source.with_suffix('.bsp'):original_bsp,
                     source.with_suffix('.srf'):source.with_suffix('.srf').read_bytes(), shader_path:shader_path.read_bytes()}
        rejects('binding-'+label, ['-game', 'quake3', *base, '-light', '-fast', source], b'ERROR', protected)

    source.with_suffix('.srf').write_text(original_srf)
    for label, offset, size in (('geometry', 0, 4), ('color', 40, 1)):
        data = bytearray(original_bsp)
        vertex_lump = struct.unpack_from('<i', data, 8+10*8)[0]
        index = next(i for i, kind, *_ in paint_rows(lumps, srf) if kind != 2)
        first_vert = struct.unpack_from('<i', lumps[13], index*104+12)[0]
        location = vertex_lump+first_vert*44+offset
        if size == 4: struct.pack_into('<f', data, location, struct.unpack_from('<f', data, location)[0]+.25)
        else: data[location] ^= 1
        source.with_suffix('.bsp').write_bytes(data)
        rejects('binding-'+label, ['-game', 'quake3', *base, '-light', '-fast', source], b'binding mismatch',
                {source:original_map, source.with_suffix('.bsp'):bytes(data), shader_path:shader_path.read_bytes()})

    source.with_suffix('.bsp').write_bytes(original_bsp)
    run(compiler, ['-game', 'quake3', *base, '-onlyents', source], directory, 'onlyents')
    assert binding in payloads(source.with_suffix('.bsp').read_bytes())[0]
    run(compiler, ['-game', 'quake3', *base, '-light', '-fast', source], directory, 'onlyents-light')
    recovered, report = directory/'paint-recovered.map', directory/'paint-recovered.json'
    run(compiler, ['-game', 'quake3', *base, '-decompile', '-format', 'map', '-o', recovered,
                  '-report', report, source.with_suffix('.bsp')], directory, 'paint-decompile')
    assert b'Authored patch paint is not restored' in (directory/'paint-decompile.log').read_bytes()
    assert any('authored patch paint' in line for line in json.loads(report.read_text())['limitations'])

    # All four native Raven color channels must be bound, including styles that
    # this particular unstyled scene does not use for illumination.
    raven, raven_srf, _ = compile_case('raven-binding', good, game='ja')
    raven_bsp = source.with_suffix('.bsp').read_bytes()
    index = next(i for i, kind, *_ in paint_rows(raven, raven_srf) if kind != 2)
    first_vert = struct.unpack_from('<i', raven[13], index*148+12)[0]
    vertex_lump = struct.unpack_from('<i', raven_bsp, 8+10*8)[0]
    for style in (1, 2, 3):
        data = bytearray(raven_bsp)
        data[vertex_lump+first_vert*80+64+style*4] ^= 1
        source.with_suffix('.bsp').write_bytes(data)
        rejects('raven-binding-'+str(style), ['-game', 'ja', *base, '-light', '-fast', source], b'binding mismatch',
                {source.with_suffix('.bsp'):bytes(data)})
    result = {'matrix':records, 'malformed_sources_rejected':len(malformed), 'binding_failures_rejected':len(variants)+5,
              'legacy_reference_parity':bool(args.reference), 'shared_edge_rgb_preserved':True,
              'material_modes_kept_separate':True, 'shader_modifiers_and_unlit':True, 'onlyents_relight':True,
              'small_patch_triangles_preserved':True,
              'decompiler_warns_about_unrecovered_paint':True}
    (root/'results.json').write_text(json.dumps(result, indent=2)+'\n')
    print(f'Patch paint: {len(records)} native cases, {len(malformed)} source errors, {len(variants)+5} binding errors passed')


if __name__ == '__main__':
    main()

"""BSP-only compiled patch channel extraction, rebuilds and conservative fallbacks.
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
from patch_paint import painted


def digest(data):
    return hashlib.sha256(data).hexdigest()


def pack_bsp(original, lumps):
    result = bytearray(original[:8]+bytes(8*len(lumps)))
    for index, lump in enumerate(lumps):
        result.extend(bytes((-len(result)) % 4))
        struct.pack_into('<2i', result, 8+8*index, len(result), len(lump))
        result.extend(lump)
    return result


def read_surfaces(data):
    lumps = payloads(data)
    raven = len(lumps) == 18
    stride, vertex_stride, color_offset = (148, 80, 64) if raven else (104, 44, 40)
    origins = {0: (0., 0., 0.)}
    for entity in re.findall(rb'\{([^{}]*)\}', lumps[0]):
        keys = dict(re.findall(rb'"([^"\n]*)"\s*"([^"\n]*)"', entity))
        if keys.get(b'model', b'').startswith(b'*'):
            origins[int(keys[b'model'][1:])] = tuple(map(float, keys.get(b'origin', b'0 0 0').split()))
    owners = {}
    for model, offset in enumerate(range(0, len(lumps[7]), 40)):
        first, count = struct.unpack_from('<2i', lumps[7], offset + 24)
        owners.update((i, model) for i in range(first, first + count))
    result = []
    for number, offset in enumerate(range(0, len(lumps[13]), stride)):
        shader, _, kind, first, count, _, indices = struct.unpack_from('<7i', lumps[13], offset)
        name = lumps[1][shader*72:shader*72+64].split(b'\0')[0].decode('utf-8')
        controls = []
        for index in range(first, first+count):
            start = index * vertex_stride
            xyz = struct.unpack_from('<3f', lumps[10], start)
            st = struct.unpack_from('<2f', lumps[10], start+12)
            color = tuple(lumps[10][start+color_offset:start+color_offset+(16 if raven else 4)])
            controls.append((xyz, st, color))
        width, height = struct.unpack_from('<2i', lumps[13], offset+stride-8)
        result.append(dict(surface=number, kind=kind, shader=name, model=owners[number],
                           origin=origins[owners[number]], width=width, height=height,
                           controls=controls, indices=indices))
    return result


def read_map(path):
    records = {}
    for match in re.finditer(r'// patch (\d+)\n(?:[^\n]*Compiled[^\n]*\n)?\s*\{\s*'
                            r'(q3mapxPatchDef2|patchDef2)\s*\{([^{}]*)\}\s*\}', path.read_text(encoding='utf-8')):
        number, definition, body = match.groups()
        groups = re.findall(r'\(([^()]*)\)', body)
        width, height, *_ = map(int, groups[0].split())
        controls = [tuple(map(float, text.split())) for text in groups[1:]]
        assert len(controls) == width*height
        records[int(number)] = dict(definition=definition, width=width, height=height,
                                    controls=controls, body=body)
    return records


def channels(color, policy):
    return color[:4] if policy == 'rgba' else (255, 255, 255, color[3])


def check_export(path, original, policy, subdivisions):
    output = read_map(path)
    patches = [s for s in original if s['kind'] == 2]
    assert set(output) == {s['surface'] for s in patches}
    for patch in patches:
        record = output[patch['surface']]
        assert record['definition'] == 'q3mapxPatchDef2'
        assert (record['width'], record['height']) == (patch['width'], patch['height'])
        assert 'lightmapSampleSize 0' in record['body']
        assert f'paintSubdivisions {subdivisions}' in record['body']
        assert 'vertexRGB ' + ('material' if policy == 'rgba' else 'lighting') in record['body']
        order = [y*patch['width']+x for x in range(patch['width']) for y in range(patch['height'])]
        for control, index in zip(record['controls'], order):
            xyz, st, color = patch['controls'][index]
            expected = tuple(a+b for a, b in zip(xyz, patch['origin'])) + st + channels(color, policy)
            # Nine significant digits must round-trip the stored binary32 fields.
            assert struct.pack('<5f', *control[:5]) == struct.pack('<5f', *expected[:5]), (control, expected)
            assert control[5:] == expected[5:]
    report = json.loads(Path(str(path)+'.recovery.json').read_text(encoding='utf-8'))
    colors = report['patch_colors']
    assert colors['policy'] == policy and colors['output_subdivisions'] == subdivisions
    assert colors['counts'] == {'recovered': len(patches)}
    assert colors['basis'] == 'compiled_native_patch_control_channels'
    assert not colors['original_paint_proven'] and not colors['rebuild_equivalence_proven']
    assert colors['omitted_records'] == 0 and colors['output_sample_size'] == 0
    return patches


def check_rebuild(data, originals, policy, *, lit=False):
    surfaces = read_surfaces(data)
    native = [s for s in surfaces if s['kind'] == 2]
    assert len(native) == len(originals)
    sampled = 0
    for patch in originals:
        candidates = [s for s in native if s['model'] == patch['model'] and s['shader'] == patch['shader']]
        assert len(candidates) == 1
        rebuilt = candidates[0]
        assert (rebuilt['width'], rebuilt['height']) == (patch['width'], patch['height'])
        for before, after in zip(patch['controls'], rebuilt['controls']):
            before_world = tuple(a+b for a, b in zip(before[0], patch['origin']))
            after_world = tuple(a+b for a, b in zip(after[0], rebuilt['origin']))
            assert before_world == after_world and before[1] == after[1], (before, after)
            want = channels(before[2], policy)
            if policy == 'rgba' or not lit:
                assert after[2][:4] == want
            assert after[2][3::4] == want[3:4] * (len(after[2])//4)
        # Independent Bernstein evaluation of the recovered control field, using
        # the BSP's sampled positions to invert this fixture's linear X/Y axes.
        width, height = patch['width'], patch['height']
        x0, y0, _ = patch['controls'][0][0]
        dx = patch['controls'][1][0][0]-x0
        dy = patch['controls'][width][0][1]-y0
        render = [s for s in surfaces if s['kind'] in (1, 3) and s['model'] == patch['model'] and s['shader'] == patch['shader']]
        assert render
        for surface in render:
            for xyz, st, color in surface['controls']:
                xyz = tuple(v+a-b for v, a, b in zip(xyz, surface['origin'], patch['origin']))
                tx, ty = (xyz[0]-x0)/(2*dx), (xyz[1]-y0)/(2*dy)
                sx, sy = min(math.floor(tx), (width-3)//2), min(math.floor(ty), (height-3)//2)
                u, v = tx-sx, ty-sy
                assert 0 <= u <= 1 and 0 <= v <= 1
                bx, by = ((1-u)**2, 2*u*(1-u), u*u), ((1-v)**2, 2*v*(1-v), v*v)
                sums = [0.]*9
                for j in range(3):
                    for i in range(3):
                        position, uv, rgba = patch['controls'][(sy*2+j)*width+sx*2+i]
                        values = position+uv+channels(rgba, policy)
                        for k, value in enumerate(values):
                            sums[k] += bx[i]*by[j]*value
                assert all(abs(a-b) < 2e-5 for a, b in zip((*xyz, *st), sums[:5]))
                want = tuple(math.floor(c+.5+1e-8) for c in sums[5:])
                if policy == 'rgba' or not lit:
                    assert color[:4] == want, (xyz, color, want)
                assert color[3::4] == want[3:4] * (len(color)//4)
                sampled += 1
    return sampled


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--work-dir', required=True, type=Path)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    source = create_fixture(root, patch=False)
    plain = source.read_text(encoding='utf-8')
    shaders = source.parent.parent/'scripts/q3mapx_tests.shader'
    initial_shaders = shaders.read_text(encoding='utf-8')
    raven = source.parent.parent/'shaders'
    raven.mkdir(exist_ok=True)
    (raven/'shaderlist.txt').write_text('q3mapx_tests\n', encoding='utf-8')

    def materials(mod='', *, stone_mod=''):
        text = initial_shaders.replace('textures/q3mapx/stone\n{', 'textures/q3mapx/stone\n{\n'+stone_mod)
        text += '\ntextures/q3mapx/paint\n{\n'+mod+'\n{ map $whiteimage rgbGen vertex alphaGen vertex }\n}\n'
        text += '\ntextures/q3mapx/replacement\n{\n{ map $whiteimage }\n}\n'
        shaders.write_text(text, encoding='utf-8')
        (raven/'q3mapx_tests.shader').write_text(text, encoding='utf-8')
    materials()
    formats = ('map', 'map_bp', 'map_220')
    quality = (1, 2, 4, 8, 16, 32)
    records, defaults, skips, failures = [], [], [], []
    carriers = {}
    total_samples = 0

    for game in ('quake3', 'ja'):
        base = ['-game', game, '-fs_basegame', 'baseq3', '-fs_basepath', root, '-fs_homepath', root/'home', '-threads', 1]
        for mode in ('legacy', 'material', 'lighting'):
            for curved in (False, True):
                label = f'{game}-{mode}-'+('curved' if curved else 'multispan')
                def make_patch(origin):
                    text = painted(mode='material' if mode == 'legacy' else mode, curved=curved,
                                   width=3 if curved else 5, extent=64, origin=origin)
                    # Absolute, fractional UV offsets exercise the precise writer.
                    text = re.sub(r'\( ([^()]+) \)', lambda m: '( '+' '.join(
                        str(float(t)+(2.375 if n == 3 else -.12345679 if n == 4 else 0))
                        if n in (3, 4) else t for n, t in enumerate(m[1].split()))+' )'
                        if len(m[1].split()) == 9 else m[0], text)
                    if mode == 'legacy':
                        text = text.replace('q3mapxPatchDef2', 'patchDef2')
                        text = re.sub(r'^(lightmapSampleSize|vertexRGB|paintSubdivisions)[^\n]*\n', '', text, flags=re.M)
                        text = re.sub(r'\( ([^()]+) \)', lambda m: '( '+' '.join(m[1].split()[:5])+' )', text)
                    return text
                text = plain.replace('"message" "q3mapx regression"\n', '"message" "q3mapx regression"\n'+make_patch((-224, -224, 128)))
                text = text.replace('"targetname" "test_door"\n', '"targetname" "test_door"\n'+make_patch((96, -48, 144)))
                source.write_text(text, encoding='utf-8')
                run(exe, [*base, source], root, label+'-bsp')
                snapshots = [('bsp', source.with_suffix('.bsp').read_bytes())]
                run(exe, [*base, '-light', '-fast', source], root, label+'-light')
                snapshots.append(('light', source.with_suffix('.bsp').read_bytes()))
                source.write_text('poisoned adjacent MAP: must never be read\n', encoding='utf-8')
                source.with_suffix('.srf').write_text('poisoned adjacent SRF: must never be read\n', encoding='utf-8')
                for stage, data in snapshots:
                    bsp = source.with_name(label+'-'+stage+'.bsp')
                    bsp.write_bytes(data)
                    bsp.with_suffix('.map').write_bytes(source.read_bytes())
                    bsp.with_suffix('.srf').write_bytes(source.with_suffix('.srf').read_bytes())
                    original = read_surfaces(data)
                    if mode == 'material' and curved and stage == 'bsp': carriers[game] = (base, bsp, data)
                    for policy in ('alpha', 'rgba'):
                        n = len(records)
                        subdivisions = quality[n % len(quality)]
                        use_default = n % 7 == 0
                        if use_default: subdivisions = 16
                        output = bsp.with_name(label+'-'+stage+'-'+policy+'.map')
                        argv = [*base, '-convert' if n % 2 else '-decompile', '-format', formats[n % 3],
                                '-patch-colors', policy, '-o', output]
                        if not use_default: argv += ['-patch-color-subdivisions', subdivisions]
                        if n % 5 == 0: argv += ['-fast']
                        run(exe, [*argv, bsp], root, output.stem+'-recover')
                        patches = check_export(output, original, policy, subdivisions)
                        assert len(patches) == 2
                        run(exe, [*base, '-threads', 4, output], root, output.stem+'-rebuild')
                        samples = check_rebuild(output.with_suffix('.bsp').read_bytes(), patches, policy)
                        total_samples += samples
                        if stage == 'light':
                            for attempt in range(2):
                                run(exe, [*base, '-light', '-fast', output], root, output.stem+f'-relight-{attempt}')
                                check_rebuild(output.with_suffix('.bsp').read_bytes(), patches, policy, lit=True)
                        assert bsp.read_bytes() == data
                        assert bsp.with_suffix('.map').read_bytes() == source.read_bytes()
                        assert bsp.with_suffix('.srf').read_bytes() == source.with_suffix('.srf').read_bytes()
                        records.append(dict(case=output.stem, format=formats[n % 3], subdivisions=subdivisions,
                                            patches=2, sampled_vertices=samples, repeated_relight=stage == 'light',
                                            input_sha256=digest(data)))
                    if stage == 'light':
                        for fmt in formats:
                            output = bsp.with_name(label+'-default-'+fmt+'.map')
                            argv = [*base, '-decompile', '-format', fmt, '-o', output, bsp]
                            run(exe, argv, root, output.stem)
                            expected = output.read_bytes()
                            report_path = Path(str(output)+'.recovery.json')
                            expected_report = report_path.read_bytes()
                            assert b'q3mapxPatchDef2' not in expected
                            assert 'patch_colors' not in json.loads(expected_report)
                            run(exe, [*argv[:-1], '-patch-colors', 'none', bsp], root, output.stem+'-none')
                            assert output.read_bytes() == expected and report_path.read_bytes() == expected_report
                            if args.reference:
                                run(args.reference.resolve(), argv, root, output.stem+'-reference')
                                assert output.read_bytes() == expected and report_path.read_bytes() == expected_report
                            defaults.append(dict(case=output.stem, reference_parity=bool(args.reference)))

    def recover_case(game, label, expected, *, policy='rgba', data=None, extra=()):
        base, carrier, original = carriers[game]
        bsp = carrier.with_name(label+'.bsp')
        bsp.write_bytes(data if data is not None else original)
        output = bsp.with_suffix('.map')
        run(exe, [*base, '-decompile', '-patch-colors', policy, *extra, '-o', output, bsp], root, label)
        report = json.loads(Path(str(output)+'.recovery.json').read_text(encoding='utf-8'))
        assert report['patch_colors']['counts'] == expected, (label, report['patch_colors'])
        records = read_map(output)
        statuses = {r['surface']: r['status'] for r in report['patch_colors']['patches']}
        for surface, patch in records.items():
            assert (patch['definition'] == 'q3mapxPatchDef2') == (statuses[surface] == 'recovered')
        if records and expected == {'recovered': len(records)}:
            check_export(output, read_surfaces(bsp.read_bytes()), policy, 16)
        skips.append(dict(case=label, counts=expected))

    for game in ('quake3', 'ja'):
        for label, mod, reason in (
            ('rgb-mod', 'q3map_colorMod scale ( 0.5 1 0.25 )', 'material_color_modifier'),
            ('alpha-mod', 'q3map_alphaMod scale 0.5', 'material_color_modifier'),
            ('indexed', 'q3map_indexed', 'incompatible_paint_material'),
            ('sprite', 'deformVertexes autosprite', 'incompatible_paint_material'),
            ('invert', 'q3map_invert', 'material_geometry_or_uv_modifier'),
            ('offset', 'q3map_offset 1', 'material_geometry_or_uv_modifier'),
            ('tcgen', 'q3map_tcGen vector ( 1 0 0 ) ( 0 1 0 )', 'material_geometry_or_uv_modifier'),
            ('tcmod', 'q3map_tcMod translate 2 3', 'material_geometry_or_uv_modifier'),
            ('redirect', 'q3map_deprecateShader textures/q3mapx/replacement', 'material_redirect')):
            materials(mod)
            recover_case(game, game+'-'+label, {reason: 2})
        materials(stone_mod='q3map_alphaMod volume\nq3map_alphaMod scale 0.5\n')
        recover_case(game, game+'-volume', {'color_modifier_volume_present': 2})
        materials()

        base = carriers[game][0]
        source.write_text(plain.replace('"message" "q3mapx regression"\n',
                                      '"message" "q3mapx regression"\n'+patch(31, 31)), encoding='utf-8')
        run(exe, [*base, source], root, game+'-large-grid-bsp')
        large = source.with_suffix('.bsp').read_bytes()
        recover_case(game, game+'-large-grid-32', {'paint_grid_limit': 1}, data=large,
                     extra=['-patch-color-subdivisions', 32])
        recover_case(game, game+'-large-grid-16', {'recovered': 1}, data=large)

        # Non-solid paint can lose its entire native control grid. Do not invent
        # a patch from the surviving render triangle soup or a nearby MAP/SRF.
        materials('surfaceparm nonsolid')
        source.write_text(plain.replace('"message" "q3mapx regression"\n',
                                      '"message" "q3mapx regression"\n'+painted()), encoding='utf-8')
        run(exe, [*base, source], root, game+'-triangle-only-bsp')
        triangle_only = source.with_suffix('.bsp').read_bytes()
        assert not any(s['kind'] == 2 for s in read_surfaces(triangle_only))
        assert any(s['shader'] == 'textures/q3mapx/paint' for s in read_surfaces(triangle_only))
        recover_case(game, game+'-triangle-only', {}, data=triangle_only)
        materials()

        # Names outside textures/ cannot round-trip through the patch MAP parser.
        raw = carriers[game][2]
        data = bytearray(raw)
        shader_start, shader_size = struct.unpack_from('<2i', raw, 16)
        for offset in range(shader_start, shader_start+shader_size, 72):
            if data[offset:offset+64].split(b'\0')[0] == b'textures/q3mapx/paint':
                data[offset:offset+64] = b'models/paint'.ljust(64, b'\0')
        recover_case(game, game+'-material-name', {'unsupported_material_name': 2}, data=data)

        recover_case(game, game+'-triangle-uv-policy', {'recovered': 2}, extra=['-uv-policy', 'triangle'])
        data = bytearray(raw)
        vertex_offset = struct.unpack_from('<i', data, 8+10*8)[0]
        surface_offset = struct.unpack_from('<i', data, 8+13*8)[0]
        door = next(s for s in read_surfaces(raw) if s['kind'] == 2 and s['model'] == 1)
        first = struct.unpack_from('<i', data, surface_offset+door['surface']*(148 if game == 'ja' else 104)+12)[0]
        struct.pack_into('<f', data, vertex_offset+first*(80 if game == 'ja' else 44), 65536.)
        recover_case(game, game+'-translated-coordinate-limit', {'recovered': 1, 'source_coordinate_limit': 1}, data=data)

    # Different active RBSP RGB must not be silently flattened. Alpha remains
    # representable when only RGB differs; inactive storage is not a style.
    _, _, raw = carriers['ja']
    offsets = [struct.unpack_from('<2i', raw, 8+i*8) for i in range(18)]
    for label, active, component, policy, expected in (
        ('rgb-style-conflict', True, 0, 'rgba', 'conflicting_active_styles'),
        ('alpha-style-rgb-only', True, 0, 'alpha', 'recovered'),
        ('alpha-style-conflict', True, 3, 'alpha', 'conflicting_active_styles'),
        ('inactive-style', False, 3, 'rgba', 'recovered'),
        ('equal-style', True, None, 'rgba', 'recovered')):
        data = bytearray(raw)
        for s in read_surfaces(data):
            if s['kind'] != 2: continue
            surface = offsets[13][0]+s['surface']*148
            data[surface+33] = 1 if active else 255
            first = struct.unpack_from('<i', data, surface+12)[0]
            if component is not None:
                vertex = offsets[10][0]+first*80+64
                data[vertex+4+component] = (data[vertex+component]+1) % 256
        recover_case('ja', label, {expected: 2}, policy=policy, data=data)

    data = bytearray(raw)
    first_patch = next(s for s in read_surfaces(raw) if s['kind'] == 2)
    surface = offsets[13][0]+first_patch['surface']*148
    data[surface+33] = 1
    first = struct.unpack_from('<i', data, surface+12)[0]
    vertex = offsets[10][0]+first*80+64
    data[vertex+4] = (data[vertex]+1) % 256
    recover_case('ja', 'mixed-style-results', {'recovered': 1, 'conflicting_active_styles': 1}, data=data)

    # Report truncation must not truncate the actual MAP or aggregate counts.
    base, carrier, raw = carriers['quake3']
    lumps = [bytearray(p) for p in payloads(raw)]
    first, count = struct.unpack_from('<2i', lumps[7], 24)
    insertion, copies = first+count, 10_005
    native = next(s['surface'] for s in read_surfaces(raw) if s['kind'] == 2 and s['model'] == 0)
    duplicate = lumps[13][native*104:(native+1)*104]
    lumps[13][insertion*104:insertion*104] = duplicate*copies
    struct.pack_into('<i', lumps[7], 28, count+copies)
    for offset in range(40, len(lumps[7]), 40):
        old_first = struct.unpack_from('<i', lumps[7], offset+24)[0]
        if old_first >= insertion: struct.pack_into('<i', lumps[7], offset+24, old_first+copies)
    for offset in range(0, len(lumps[5]), 4):
        surface = struct.unpack_from('<i', lumps[5], offset)[0]
        if surface >= insertion: struct.pack_into('<i', lumps[5], offset, surface+copies)
    many = carrier.with_name('many-native-patches.bsp')
    many.write_bytes(pack_bsp(raw, lumps))
    output = many.with_suffix('.map')
    run(exe, [*base, '-decompile', '-fast', '-patch-colors', 'alpha', '-o', output, many], root, 'record-limit')
    colors = json.loads(Path(str(output)+'.recovery.json').read_text(encoding='utf-8'))['patch_colors']
    assert colors['counts'] == {'recovered': copies+2}
    assert len(colors['patches']) == colors['record_limit'] == 10_000 and colors['omitted_records'] == 7
    assert output.read_bytes().count(b'q3mapxPatchDef2') == copies+2
    skips.append(dict(case='record-limit', counts=colors['counts'], omitted_records=7))

    # CLI mistakes and publication failures preserve earlier work and inputs.
    base, bsp, data = carriers['quake3']
    output, report = root/'preserved.map', root/'preserved.json'
    for label, options, message in (
        ('bad-policy', ['-patch-colors', 'paint'], b'Patch colors must'),
        ('missing-policy', ['-patch-colors'], b'No parameters specified'),
        ('bad-quality', ['-patch-colors', 'alpha', '-patch-color-subdivisions', 3], b'Patch color subdivisions'),
        ('quality-only', ['-patch-color-subdivisions', 8], b'requires -patch-colors'),
        ('zero-quality', ['-patch-colors', 'alpha', '-patch-color-subdivisions', 0], b'integer in 1..32'),
        ('large-quality', ['-patch-colors', 'rgba', '-patch-color-subdivisions', 64], b'integer in 1..32'),
        ('mesh-output', ['-format', 'obj', '-patch-colors', 'alpha'], b'requires MAP export'),
        ('map-input', ['-readmap', '-patch-colors', 'alpha'], b'compiled BSP input'),
        ('replace-material', ['-wtf', '-patch-colors', 'rgba'], b'material replacement'),
    ):
        output.write_bytes(b'previous MAP'); report.write_bytes(b'previous report')
        result = subprocess.run([str(exe), *map(str, [*base, '-decompile', '-o', output, '-report', report, *options, bsp])],
                                cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        (root/(label+'.log')).write_bytes(result.stdout)
        assert result.returncode == 1 and message.lower() in result.stdout.lower(), (label, result.stdout[-3000:])
        assert output.read_bytes() == b'previous MAP' and report.read_bytes() == b'previous report'
        assert bsp.read_bytes() == data
        failures.append(label)
    blocked = root/'directory-report.json'
    blocked.mkdir(exist_ok=True)
    (blocked/'preserved').write_bytes(b'previous directory')
    result = subprocess.run([str(exe), *map(str, [*base, '-decompile', '-patch-colors', 'rgba', '-o', output, '-report', blocked, bsp])],
                            cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    (root/'report-directory.log').write_bytes(result.stdout)
    assert result.returncode == 1 and output.read_bytes() == b'previous MAP'
    assert (blocked/'preserved').read_bytes() == b'previous directory'
    assert not list(root.rglob('*.q3mapx-*.tmp'))
    failures.append('report-directory')

    evidence = dict(schema_version=1, compiler=str(exe), recovery_cases=records, default_cases=defaults,
                    guarded_cases=skips, failures=failures, sampled_vertices=total_samples,
                    source_map_and_srf_unused=True, input_bsp_unchanged=True)
    (root/'validation.json').write_text(json.dumps(evidence, indent=2)+'\n', encoding='utf-8')
    print(f'{len(records)} extraction/rebuild cases, {total_samples} analytic samples, '
          f'{len(defaults)} default parity cases, {len(skips)} guarded cases, {len(failures)} failures passed')


if __name__ == '__main__':
    main()

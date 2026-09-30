"""Absolute native UV and patch precision through all MAP writers and rebuilds.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from fixtures import create_fixture
from integration import run
from patch_input import payloads
from uv_recovery import model_origin, pack, sizes, solid_signature


def surfaces(data):
    parts = payloads(data)
    stride, surface_stride, normal_offset = sizes(data)
    for model in range(len(parts[7])//40):
        first, count = struct.unpack_from('<2i', parts[7], model*40+24)
        origin = model_origin(parts, model)
        for index in range(first, first+count):
            kind, start, length = struct.unpack_from('<3i', parts[13], index*surface_stride+8)
            vertices = [struct.unpack_from('<5f',parts[10],(start+i)*stride) for i in range(length)]
            normals = [struct.unpack_from('<3f',parts[10],(start+i)*stride+normal_offset) for i in range(length)]
            yield model, index, kind, start, origin, vertices, normals


def fixture(data, mode):
    parts = list(payloads(data)); stride, _, _ = sizes(data)
    vertices = bytearray(parts[10])
    scale, shift = {
        'offsets': ((1/32, -1/64), (7.25, -3.125)),
        # Both coordinates stay within (0, 1): this isolates decimal loss from
        # integer wrapping, which has its own signed-offset fixture.
        'fine-gradient': ((1e-10, -2e-10), (2e-7, 7e-8)),
        'fine-scale': ((2**24, -2**25), (0, 0)),
        'large-offsets': ((1/8, -1/4), (2**20+.125, -2**19-.0625)),
        'overflow-shift': ((1e35, -1/64), (2e38, -3.125)),
    }[mode]
    for model, index, kind, start, origin, points, normals in surfaces(data):
        if kind == 1:
            dominant = max(range(3), key=lambda i: abs(normals[0][i]))
            axes = [i for i in range(3) if i != dominant]
            for i, point in enumerate(points):
                uv = [scale[j]*(point[axes[j]]+origin[axes[j]])+shift[j] for j in range(2)]
                struct.pack_into('<2f', vertices, (start+i)*stride+12, *uv)
        elif kind == 2:
            for i, point in enumerate(points):
                # A real native patch retains source control points, including
                # values smaller than the former six-decimal serialization.
                z = 1.23456789e-7 if i == 0 and model == 0 else point[2]
                struct.pack_into('<3f', vertices, (start+i)*stride+8, z, 2e-8*(i+1), -5e-9*(i+1))
    parts[10] = vertices
    return pack(data, parts)


def brush_samples(data):
    result = {}
    for model, _, kind, _, _, points, normals in surfaces(data):
        if kind != 1: continue
        for point, normal in zip(points, normals):
            key = (model, *point[:3], *normal)
            assert key not in result or result[key] == point[3:]
            result[key] = point[3:]
    return result


def patches(data):
    return sorted((model, tuple(points)) for model, _, kind, _, _, points, _ in surfaces(data) if kind == 2)


def errors(source, rebuilt):
    wanted, actual = brush_samples(source), brush_samples(rebuilt)
    assert wanted.keys() == actual.keys(), 'Native visible brush sample positions/normals changed'
    maximum, ratio = 0, 0
    for key, uv in wanted.items():
        for a,b in zip(actual[key],uv):
            error = abs(a-b)
            # Absolute UVs: neither uniform integer shifts nor per-vertex
            # modulo matching is allowed. Binary32 arithmetic gets an explicit
            # scale-dependent allowance, including very small coordinates.
            tolerance = max(1e-13, 16*2**-23*abs(b))
            maximum = max(maximum,error); ratio = max(ratio,error/tolerance)
    return {'max_error_repeats': maximum, 'max_tolerance_ratio': ratio,
            'samples': len(wanted), 'models': sorted({key[0] for key in wanted})}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', required=True, type=Path)
    p.add_argument('--reference', type=Path)
    p.add_argument('--work-dir', required=True, type=Path)
    a = p.parse_args(); compiler, root = a.compiler.resolve(), a.work_dir.resolve()
    reference = a.reference.resolve() if a.reference else None
    results, controls = [], []
    for game in ('quake3', 'ja'):
        directory = root/game
        shader_directory = 'shaders' if game == 'ja' else 'scripts'
        source = create_fixture(directory, shader_directory=shader_directory)
        shader = directory/'baseq3'/shader_directory/'q3mapx_tests.shader'
        text = shader.read_text().replace('qer_editorimage', 'q3map_globaltexture\n    qer_editorimage')
        text = text.replace('map textures/q3mapx/checker.tga', 'clampmap textures/q3mapx/checker.tga')
        if game == 'ja': text += '\ntextures/system/origin\n{\n surfaceparm origin\n surfaceparm nodraw\n}\n'
        shader.write_text(text)
        base = ['-game',game,'-fs_basegame','baseq3','-fs_basepath',directory,'-fs_homepath',directory/'home','-threads',1]
        run(compiler,[*base,'-meta',source],directory,'prepare')
        carrier = source.with_suffix('.bsp').read_bytes()
        assert model_origin(payloads(carrier),1) == [112,0,64]
        for mode in ('offsets','fine-gradient','fine-scale','large-offsets'):
            native = fixture(carrier,mode); path = directory/(mode+'.bsp'); path.write_bytes(native)
            for fmt in ('map','map_bp','map_220'):
                label = mode+'-'+fmt; output = directory/(label+'.map')
                run(compiler,[*base,'-decompile','-format',fmt,'-o',output,path],directory,label)
                report = json.loads(Path(str(output)+'.recovery.json').read_text())
                assert report['uv_output']['policy'] == 'preserve_offsets_and_precision'
                assert report['uv_output']['preserves_integer_offsets']
                assert report['uv_output']['unrepresentable_valve_faces'] == 0
                assert report['matched_uv_faces']+report['fallback_uv_faces'] == report['faces']
                run(compiler,[*base,'-meta',output],directory,label+'-rebuild')
                rebuilt = output.with_suffix('.bsp').read_bytes()
                assert solid_signature(native) == solid_signature(rebuilt), (game,label,'brush solids changed')
                error = errors(native,rebuilt)
                assert error['models'] == [0,1]
                assert error['max_tolerance_ratio'] <= 1, (game,label,error)
                assert patches(native) == patches(rebuilt), (game,label,'patch controls or UVs rounded')
                record = {'game':game,'case':mode,'format':fmt,'absolute_uv_error':error,
                          'native_sha256':hashlib.sha256(native).hexdigest(),'brush_solids_preserved':True,
                          'patch_control_positions_and_uvs_exact':True}
                if reference:
                    old = directory/(label+'-before.map')
                    run(reference,[*base,'-decompile','-format',fmt,'-o',old,path],directory,label+'-before')
                    run(compiler,[*base,'-meta',old],directory,label+'-before-rebuild')
                    old_bsp = old.with_suffix('.bsp').read_bytes()
                    record['previous_absolute_uv_error'] = errors(native,old_bsp)
                    record['previous_patch_controls_exact'] = patches(native) == patches(old_bsp)
                    assert not record['previous_patch_controls_exact']
                    if mode in ('offsets','large-offsets') or (mode == 'fine-gradient' and fmt == 'map_bp') or (mode == 'fine-scale' and fmt != 'map_bp'):
                        assert record['previous_absolute_uv_error']['max_tolerance_ratio'] > 100, record
                    legacy = directory/(label+'-triangle.map')
                    old_legacy = directory/(label+'-triangle-before.map')
                    for exe, out in ((compiler,legacy),(reference,old_legacy)):
                        run(exe,[*base,'-decompile','-format',fmt,'-uv-policy','triangle','-o',out,path],directory,out.stem)
                    assert legacy.read_bytes() == old_legacy.read_bytes(), (game,label,'compatibility output changed')
                    record['triangle_reference_map_parity'] = True
                if mode == 'offsets':
                    parallel = directory/(label+'-workers4.map')
                    run(compiler,[*base[:-1],4,'-decompile','-format',fmt,'-o',parallel,path],directory,parallel.stem)
                    assert parallel.read_bytes() == output.read_bytes(), (game,label,'worker difference')
                assert path.read_bytes() == native
                results.append(record)
        # Pixel shifts have less representable range than repeat offsets. A
        # diagnosed finite fallback must remain readable and preserve solids.
        native = fixture(carrier,'overflow-shift')
        path = directory/'overflow-shift.bsp'; path.write_bytes(native)
        output = directory/'overflow-shift-recovered.map'
        run(compiler,[*base,'-decompile','-o',output,path],directory,'overflow-shift')
        report = json.loads(Path(str(output)+'.recovery.json').read_text())
        count = report['uv_output']['unrepresentable_valve_faces']
        assert count > 0 and report['matched_uv_faces']+report['fallback_uv_faces'] == report['faces']
        assert b'Valve texture parameters exceed MAP storage' in (directory/'overflow-shift.log').read_bytes()
        run(compiler,[*base,'-meta',output],directory,'overflow-shift-rebuild')
        assert solid_signature(native) == solid_signature(output.with_suffix('.bsp').read_bytes())
        assert path.read_bytes() == native
        controls.append({'game':game,'case':'unrepresentable_valve_shift','fallback_faces':count,'brush_solids_preserved':True})
        native = fixture(carrier,'offsets')
        path = directory/'fast-patches.bsp'; path.write_bytes(native)
        for fmt in ('map','map_bp','map_220'):
            label = 'fast-patches-'+fmt; output = directory/(label+'.map')
            run(compiler,[*base,'-decompile','-fast','-format',fmt,'-o',output,path],directory,label)
            run(compiler,[*base,'-meta',output],directory,label+'-rebuild')
            assert patches(native) == patches(output.with_suffix('.bsp').read_bytes()), (game,label)
            assert not json.loads(Path(str(output)+'.recovery.json').read_text())['uv_output']['preserves_integer_offsets']
            controls.append({'game':game,'case':label,'patch_control_positions_and_uvs_exact':True})
    (root/'validation.json').write_text(json.dumps({'compiler_sha256':hashlib.sha256(compiler.read_bytes()).hexdigest(),
        'reference_sha256':hashlib.sha256(reference.read_bytes()).hexdigest() if reference else None,
        'cases':results,'controls':controls},indent=2)+'\n')
    print(f'{len(results)} absolute UV/patch rebuild cases and {len(controls)} representation/fast controls passed across IBSP/RBSP and all MAP writers')


if __name__ == '__main__': main()

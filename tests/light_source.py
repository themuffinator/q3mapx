"""LIGHT source validation must not depend on discarded brush materials.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from brush_input import adapt, edit_side
from fixtures import create_fixture
from integration import run
from patch_input import patch, payloads
from patch_paint import painted
from surface_density import authored_brush, authored_patch


def digest(data):
    return hashlib.sha256(data).hexdigest()


def source_text(plain, style, authored):
    if authored:
        return re.sub(r'\{\n(?:\( [^\n]+\n)+\}\n',
                      lambda m: authored_brush(m[0], style, [8]*m[0].count('\n( ')), plain)
    return adapt(plain, style)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--work-dir', type=Path, required=True)
    args = parser.parse_args()
    compiler, root = args.compiler.resolve(), args.work_dir.resolve()
    reference = args.reference.resolve() if args.reference else None
    root.mkdir(parents=True, exist_ok=True)
    records, rejected, negative_controls = [], [], []
    world_end = '\n}\n{\n"classname" "info_player_deathmatch"'
    for game in ('quake3', 'ja'):
        directory = root / game
        source = create_fixture(directory, patch=False)
        plain = source.read_text(encoding='utf-8')
        shaders = directory / 'baseq3/scripts/q3mapx_tests.shader'
        original_shader = shaders.read_text(encoding='utf-8')
        raven = directory / 'baseq3/shaders'
        raven.mkdir(exist_ok=True)
        (raven/'shaderlist.txt').write_text('q3mapx_tests\n', encoding='utf-8')
        unused = ('textures/q3mapx/unused\n{\n qer_editorimage textures/q3mapx/checker.tga\n'
                  ' { map textures/q3mapx/checker.tga }\n}\n')
        base = ['-game', game, '-fs_basegame', 'baseq3', '-fs_basepath', directory,
                '-fs_homepath', directory/'home', '-threads', 1]
        command = [*base, '-light', '-fast', source]
        (raven/shaders.name).write_text(original_shader, encoding='utf-8')
        run(compiler, [*base, source], directory, 'carrier')
        carrier = source.with_suffix('.bsp').read_bytes()
        srf = source.with_suffix('.srf').read_bytes()
        # Geometry comes from the BSP. Only these point lights need to be read
        # from the MAP; their unmodified form is an independent no-brush oracle.
        light_entities = re.findall(r'\{\n"classname" "light"\n.*?\n\}\n', plain, re.S)
        assert len(light_entities) == 1
        lights_only = '{\n"classname" "worldspawn"\n}\n' + ''.join(light_entities)
        patched_sources = {}
        for style in ('quake', 'bp', 'valve'):
            for authored in (False, True):
                text = source_text(plain, style, authored)
                assert text.count('q3mapxBrushDef1') == (9 if authored else 0)
                text = text.replace('q3mapx/stone', 'q3mapx/source-only-missing')
                # All native patch source formats still parse when their mesh
                # is discarded. Their material need not exist in the BSP/VFS.
                assert text.count(world_end) == 1
                text = text.replace(world_end, '\n'+patch()+authored_patch(12)+painted()+world_end)
                patched_sources[(style,authored)] = text
        for shader_case, definition in (('used-first',original_shader),
                                        ('unused-first',unused+original_shader), ('none','')):
            shaders.write_text(definition, encoding='utf-8')
            (raven/shaders.name).write_text(definition, encoding='utf-8')
            for sidecar in ('resolved', 'without-materials'):
                current_srf = srf if sidecar == 'resolved' else re.sub(rb'^\s*shader\s+[^\n]*\n',b'',srf,flags=re.M)
                if sidecar != 'resolved': assert current_srf != srf
                source.with_suffix('.srf').write_bytes(current_srf)
                source.write_text(lights_only, encoding='utf-8')
                source.with_suffix('.bsp').write_bytes(carrier)
                label = f'{game}-{shader_case}-{sidecar}'
                run(compiler, command, directory, label+'-oracle')
                expected = payloads(source.with_suffix('.bsp').read_bytes())
                assert expected[14] and max(expected[14]) > 0, 'Oracle must actually light the BSP'
                if reference:
                    source.with_suffix('.bsp').write_bytes(carrier)
                    run(reference, command, directory, label+'-reference')
                    assert payloads(source.with_suffix('.bsp').read_bytes()) == expected, 'No-brush legacy lighting changed'
                for (style,authored), text in patched_sources.items():
                    name = label+'-'+style+('-authored' if authored else '-legacy')
                    source.write_text(text, encoding='utf-8')
                    source.with_suffix('.bsp').write_bytes(carrier)
                    run(compiler, command, directory, name)
                    result = payloads(source.with_suffix('.bsp').read_bytes())
                    assert result == expected, (name, 'Discarded MAP geometry changed LIGHT output')
                    assert source.read_text(encoding='utf-8') == text
                    assert source.with_suffix('.srf').read_bytes() == current_srf
                    records.append({'case':name,'all_lump_parity':True,
                                    'lightmaps_sha256':digest(result[14]),'reference_oracle_parity':bool(reference)})
                source.write_text('{\n"classname" "worldspawn"\n}\n', encoding='utf-8')
                source.with_suffix('.bsp').write_bytes(carrier)
                run(compiler, command, directory, label+'-no-lights')
                assert payloads(source.with_suffix('.bsp').read_bytes())[14] != expected[14], 'MAP light entities must affect the bake'
                negative_controls.append(label)
        # Cold unrelated shader remains first during all rejection cases. A
        # shader-initialization failure must not mask the actual bad source.
        shaders.write_text(unused+original_shader, encoding='utf-8')
        (raven/shaders.name).write_text(unused+original_shader, encoding='utf-8')
        for (style,authored), text in patched_sources.items():
            first = next(line for line in text.splitlines() if line.startswith('( ') and 'source-only-missing' in line).split()
            fields = {'quake':16,'bp':17,'valve':17}
            variants = [('point',edit_side(text,{1:'7junk'})),
                        ('coordinate',edit_side(text,{fields[style]:'nan'})),
                        ('flags',edit_side(text,{len(first)-(5 if authored else 3):'1junk'})),
                        ('truncated',text[:text.index(' q3mapx/source-only-missing')])]
            if style != 'bp':
                variants.append(('derived-axis',edit_side(text,{19 if style=='quake' else 29:'1e-40'})))
            if authored:
                variants.append(('density',text.replace('lightmapSampleSize 8','lightmapSampleSize -1',1)))
            for kind, bad in variants:
                name=f'{game}-{style}-{authored}-{kind}'
                source.write_text(bad, encoding='utf-8')
                saved={source.with_suffix('.bsp'):carrier,source.with_suffix('.srf'):srf}
                for suffix in ('.prt','.lin','.reg','.obj','.mtl'):
                    saved[source.with_suffix(suffix)]=f'previous {suffix} output\n'.encode()
                for path,data in saved.items(): path.write_bytes(data)
                result=subprocess.run([str(compiler),*map(str,command)],cwd=directory,capture_output=True,timeout=30)
                log=result.stdout+result.stderr
                (directory/(name+'.log')).write_bytes(log)
                expected_error = {'point':b'finite plane point',
                    'coordinate':{'quake':b'finite representable texture shift',
                                  'bp':b'finite representable texture matrix component',
                                  'valve':b'finite representable Valve texture axis/shift'}[style],
                    'flags':b'32-bit decimal content flags', 'truncated':b'is incomplete',
                    'derived-axis':b'finite derived texture mapping', 'density':b'lightmap sample size'}[kind]
                assert result.returncode==1 and expected_error in log, (name,result.returncode,log[-1800:])
                assert b'AddressSanitizer' not in log and b'runtime error:' not in log, name
                assert source.read_text(encoding='utf-8')==bad
                assert all(path.read_bytes()==data for path,data in saved.items()), (name,'Previous output changed')
                rejected.append(name)
    report={'compiler_sha256':digest(compiler.read_bytes()),
            'reference_sha256':digest(reference.read_bytes()) if reference else None,
            'cases':records,'no_lights_negative_controls':negative_controls,
            'rejected':rejected,'previous_outputs_preserved':True}
    (root/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'LIGHT source: {len(records)} equivalent-source cases, {len(negative_controls)} light controls, {len(rejected)} rejected sources passed')


if __name__=='__main__': main()

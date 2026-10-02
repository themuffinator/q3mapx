"""Native inherited lightmap spacing through classification, copies and merging.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from fixtures import box, create_fixture
from integration import run
from patch_input import patch, payloads
from patch_paint import normalized
from patch_color_recovery import read_surfaces
from surface_density import authored_brush, extras


SETTINGS = [
    # name, entity base, entity scale, global base, minimum
    ('default', 0, 0, 16, 1), ('unit', 24, 1, 16, 1),
    ('scaled', 24, 2, 16, 1), ('shrunk', 24, 0.25, 16, 1),
    ('fractional', 27, 1.1, 16, 1), ('global', 0, 2, 24, 1),
    ('minimum', 24, 0.25, 16, 16), ('overflow', 24, 1e38, 16, 1),
    ('large-base', 2000000000, 0, 16, 1), ('large-minimum', 0, 0, 1048576, 1048576),
]
SIZES = dict(stone=0, sampled=32, clone=16, back=48, split=32,
             forced=32, patch=32, model=32, door=32, group=32,
             unlit=32, authored=32, decal=32)


def fixture(folder, settings):
    name, base, scale, global_size, minimum = settings
    source = create_fixture(folder, patch=False)
    game = source.parent.parent
    shader = game/'scripts/q3mapx_tests.shader'
    for material, size in SIZES.items():
        if material == 'stone': continue
        directives = [f'q3map_lightmapSampleSize {size}']
        if material == 'sampled': directives += ['q3map_cloneShader textures/q3mapx/clone', 'q3map_backShader textures/q3mapx/back']
        if material == 'split': directives += ['q3map_tessSize 16']
        if material in ('forced','decal'): directives += ['q3map_forceMeta']
        if material == 'unlit': directives += ['surfaceparm nolightmap']
        with shader.open('a', encoding='utf-8') as out:
            out.write('\ntextures/q3mapx/'+material+'\n{\nqer_editorimage textures/q3mapx/checker.tga\n'+
                      '\n'.join(directives)+'\n{ map $lightmap }\n{ map textures/q3mapx/checker.tga blendFunc filter }\n}\n')
    raven = game/'shaders'; raven.mkdir(exist_ok=True)
    for file in (game/'scripts').iterdir(): (raven/file.name).write_bytes(file.read_bytes())
    settings_text = f'"_lightmapsamplesize" "{base}"\n"_lightmapscale" "{scale}"\n'
    text = source.read_text().replace('"classname" "worldspawn"', '"classname" "worldspawn"\n'+settings_text)
    geometry = box((-224,0,0),(-160,64,64),'q3mapx/sampled')
    geometry += box((-144,0,0),(-80,64,64),'q3mapx/split')
    geometry += box((-224,96,0),(-160,160,64),'q3mapx/forced')
    geometry += box((-144,96,0),(-80,160,64),'q3mapx/unlit')
    geometry += authored_brush(box((16,-208,0),(80,-144,64),'q3mapx/authored'),'quake',[7]*6)
    geometry += patch().replace('q3mapx/stone','q3mapx/patch')
    marker = '}\n{\n"classname" "info_player_deathmatch"'
    assert text.count(marker) == 1
    text = text.replace(marker, geometry+marker)
    door = box((96,-48,16),(128,48,112))
    text = text.replace(door, door.replace('q3mapx/stone','q3mapx/door'))
    text = text.replace('"classname" "func_door"','"classname" "func_door"\n'+settings_text)
    # Adjacent coplanar group faces use the same shader but different effective
    # spacing. Their sampling must remain distinct when triangles are merged.
    group_scales = (0.5, 1.5) if scale not in (0, 1) else (1, 1)
    for i, group_scale in enumerate(group_scales):
        text += '{\n"classname" "func_group"\n'+f'"_lightmapscale" "{group_scale}"\n'+box(
            (64+i*64,96,0),(128+i*64,160,64),'q3mapx/group')+'}\n'
    # Original Y-up OBJ: the importer rotates it into a horizontal Quake plane.
    models = game/'models/q3mapx'; models.mkdir(parents=True, exist_ok=True)
    (models/'sample.obj').write_text('mtllib sample.mtl\nusemtl textures/q3mapx/model\n'
        'v 0 0 0\nv 64 0 0\nv 0 0 -64\nv 64 0 -64\n'
        'vt 0 0\nvt 1 0\nvt 0 1\nvt 1 1\nvn 0 1 0\n'
        'f 1/1/1 2/2/1 3/3/1\nf 2/2/1 4/4/1 3/3/1\n')
    (models/'sample.mtl').write_text('newmtl textures/q3mapx/model\nKd 1 1 1\n')
    text += '{\n"classname" "misc_model"\n"origin" "96 -208 96"\n"model" "models/q3mapx/sample.obj"\n"spawnflags" "4"\n'+settings_text+'}\n'
    # Triangular projected decals can enter meta merging before their first
    # classification. Zero there is an unresolved base, not final lit spacing.
    controls = '\n'.join('( '+' '.join(f'( {-48+48*x} {176+24*y} 80 {x/2} {y/2} )' for y in range(3))+' )' for x in range(3))
    text += '{\n"classname" "_decal"\n"target" "sampling_decal_target"\n{\npatchDef2\n{\nq3mapx/decal\n( 3 3 0 0 0 )\n(\n'+controls+'\n)\n}\n}\n}\n'
    text += '{\n"classname" "info_null"\n"targetname" "sampling_decal_target"\n"origin" "0 200 -16"\n}\n'
    source.write_text(text)
    return source, group_scales


def spacing(size, base, scale, global_size, minimum):
    # Independent source-level expectation: positive values truncate toward zero
    # after scaling and saturate to the supported integer interval. Chosen finite
    # fractions are away from a float rounding boundary.
    return max(min(minimum,16384), min(16384, int((size or base or global_size)*(scale or 1))))


def inspect(data, rows, settings, group_scales, patchmeta):
    _, base, scale, global_size, minimum = settings
    observed = {name:set() for name in SIZES}
    for surface in read_surfaces(data):
        index, name = surface['surface'], surface['shader'].split('/')[-1]
        assert name in SIZES, name
        value = rows[index]['sampleSize']; observed[name].add(value)
        if name == 'unlit' or (name == 'patch' and patchmeta and surface['kind'] == 2):
            expected = {0}
        elif name == 'authored': expected = {max(min(minimum,16384), 7)}
        elif name == 'decal': expected = {spacing(32,0,0,global_size,minimum)}
        elif name == 'group':
            xs = [p[0] for p,_,_ in surface['controls']]
            owners = [i for i,belongs in enumerate((min(xs)<128,max(xs)>128)) if belongs]
            assert owners, 'Unexpected interior face between adjacent solid brushes'
            expected = {spacing(32,0,group_scales[i],global_size,minimum) for i in owners}
            assert len(expected)==1, 'A surface merged across different effective densities'
        else: expected = {spacing(SIZES[name],base,scale,global_size,minimum)}
        assert value in expected, (name,value,expected,settings)
    assert all(observed.values()), observed
    assert observed['group'] == {spacing(32,0,s,global_size,minimum) for s in group_scales}
    assert observed['patch'] == ({0} if patchmeta else set()) | {spacing(32,base,scale,global_size,minimum)}
    return {name:sorted(values) for name,values in observed.items()}


def check_atlas(data, expected):
    lumps = payloads(data); raven = data[:4] == b'RBSP'
    stride, vertex, number, uv = (148,80,36,20) if raven else (104,44,28,20)
    matched = []
    for surface in read_surfaces(data):
        if surface['shader'] != 'textures/q3mapx/sampled': continue
        points = [p for p,_,_ in surface['controls']]
        if not all(abs(p[2]-64)<0.001 for p in points): continue
        offset = surface['surface']*stride
        assert struct.unpack_from('<i',lumps[13],offset+number)[0] >= 0
        first,count = struct.unpack_from('<2i',lumps[13],offset+12)
        coords = [struct.unpack_from('<2f',lumps[10],i*vertex+uv) for i in range(first,first+count)]
        spans = [(max(p[a] for p in coords)-min(p[a] for p in coords))*128 for a in range(2)]
        assert all(abs(span-64/expected)<0.001 for span in spans), (expected,spans)
        matched.append(spans)
    assert matched, 'No inherited-spacing top face was baked'
    return matched


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--case', choices=[s[0] for s in SETTINGS])
    args = parser.parse_args(); exe = args.compiler.resolve(); root = args.work_dir.resolve()
    root.mkdir(parents=True,exist_ok=True)
    records = []
    for settings in SETTINGS:
        name,base,scale,global_size,minimum = settings
        if args.case and args.case != name: continue
        for game in ('quake3','ja'):
            for mode,options in [('ordinary',[]),('meta',['-meta']),('patchmeta',['-meta','-patchmeta']),('maxarea',['-meta','-maxarea'])]:
                folder = root/f'{name}-{game}-{mode}'; source,group_scales = fixture(folder,settings)
                common = ['-game',game,'-fs_basegame','baseq3','-fs_basepath',folder,'-fs_homepath',folder/'home']
                flags = [*options,'-samplesize',global_size,'-minsamplesize',minimum]
                snapshots = []; records_by_worker = []
                for workers in (1,4):
                    command = [*common,'-threads',workers,*flags,source]
                    run(exe,command,folder,f'bsp-{workers}',timeout=180)
                    data = source.with_suffix('.bsp').read_bytes(); rows = extras(source)
                    current_srf = source.with_suffix('.srf').read_bytes()
                    observed = inspect(data,rows,settings,group_scales,mode=='patchmeta')
                    snapshot = (normalized(payloads(data)),current_srf)
                    if snapshots: assert snapshots[0] == snapshot, (settings,game,mode,'worker divergence')
                    snapshots.append(snapshot)
                    record = dict(case=name, game=game, mode=mode, workers=workers, effective_sizes=observed)
                    if args.reference and workers == 1 and name in ('default','unit','scaled'):
                        run(args.reference.resolve(),command,folder,'reference-bsp',timeout=180)
                        previous = source.with_suffix('.bsp').read_bytes(); previous_rows = extras(source)
                        if name in ('default','unit'):
                            assert snapshot == (normalized(payloads(previous)),source.with_suffix('.srf').read_bytes())
                            record['reference_native_lump_and_srf_parity'] = True
                        else:
                            # The old compiler drops scale on shader-forced meta
                            # surfaces, and on imported meshes when globally merged.
                            old = {tag:sorted({row['sampleSize'] for row in previous_rows.values()
                                   if row.get('shader')=='textures/q3mapx/'+tag}) for tag in ('forced','model','patch','sampled')}
                            assert old['forced'] == [32] and old['model'] == ([64] if mode=='ordinary' else [32]), old
                            assert observed['forced'] == observed['model'] == [64]
                            record['reference_lost_scale'] = old
                        # LIGHT must consume the current BSP and matching SRF.
                        source.with_suffix('.bsp').write_bytes(data)
                        source.with_suffix('.srf').write_bytes(current_srf)
                    # LIGHT on an imported forced-meta model currently requires
                    # global -meta; the independent ordinary-path failure is
                    # recorded separately, not treated as a sampling success.
                    if name == 'scaled' and mode != 'ordinary':
                        run(exe,[*common,'-threads',workers,'-vis','-reproducible',source],folder,f'vis-{workers}')
                        run(exe,[*common,'-threads',workers,'-light','-fast',source],folder,f'light-{workers}',timeout=240)
                        baked = source.with_suffix('.bsp').read_bytes()
                        record['packed_top_face_uv_spans'] = check_atlas(baked,64)
                    records_by_worker.append(record)
                records.extend(records_by_worker)
    result = dict(cases=records,compiler_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  reference_sha256=hashlib.sha256(args.reference.read_bytes()).hexdigest() if args.reference else None)
    (root/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'{len(records)} inherited surface-sampling builds passed; '+str(sum('packed_top_face_uv_spans' in r for r in records))+' native atlas checks')


if __name__ == '__main__': main()

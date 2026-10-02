"""Native compiler qualification for versioned per-face/per-patch texel spacing.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from brush_input import adapt
from fixtures import box, create_fixture
from integration import run
from patch_input import patch, payloads


PROJECTION = {'quake': 'quake', 'bp': 'brushPrimitives', 'valve': 'valve220'}


def authored_brush(text, style, values):
    lines = adapt(text, style).splitlines()
    if style == 'bp':
        lines[1] = 'q3mapxBrushDef1 brushPrimitives'
    else:
        lines[1:1] = ['q3mapxBrushDef1 '+PROJECTION[style], '{']
        lines.insert(-1, '}')
    side = 0
    for i, line in enumerate(lines):
        if line.startswith('( '):
            lines[i] += ' lightmapSampleSize '+str(values[side])
            side += 1
    assert side == len(values)
    return '\n'.join(lines)+'\n'


def authored_patch(size):
    return patch().replace('patchDef2', 'q3mapxPatchDef1').replace(
        '( 3 3 0 0 0 )', '( 3 3 0 0 0 )\nlightmapSampleSize '+str(size))


def extras(source):
    text = source.with_suffix('.srf').read_text(encoding='utf-8')
    sections = re.findall(r'^(default|\d+)[^\n]*\n\{\n(.*?)^\}', text, re.M | re.S)
    defaults = {}
    result = {}
    for name, body in sections:
        values = dict(defaults)
        for key, value in re.findall(r'^\s*(\w+)\s+(\S+)\s*$', body, re.M):
            values[key] = int(value) if re.fullmatch(r'-?\d+', value) else value
        if name == 'default': defaults = values
        else: result[int(name)] = values
    assert result
    return result


def geometry_for_surface(lumps, index):
    face = struct.unpack_from('<12i12f2i', lumps[13], index*104)
    points = [struct.unpack_from('<3f', lumps[10], v*44) for v in range(face[3], face[3]+face[4])]
    return face, points


def run_matrix(compiler, root, reference=None):
    root.mkdir(parents=True, exist_ok=True)
    directory = root/'native'
    source = create_fixture(directory, patch=False)
    plain = source.read_text(encoding='utf-8')
    marker = '"message" "q3mapx regression"\n'
    shader = directory/'baseq3/scripts/q3mapx_tests.shader'
    shader.write_text(shader.read_text().replace('qer_editorimage', 'q3map_lightmapSampleSize 32\n    qer_editorimage'), encoding='utf-8')
    with shader.open('a',encoding='utf-8') as out:
        out.write('\ntextures/q3mapx/unlightmapped\n{\n surfaceparm nolightmap\n { map textures/q3mapx/checker.tga }\n}\n')
    raven_shaders = directory/'baseq3/shaders'
    raven_shaders.mkdir(exist_ok=True)
    (raven_shaders/shader.name).write_bytes(shader.read_bytes())
    (raven_shaders/'shaderlist.txt').write_text('q3mapx_tests\n',encoding='utf-8')
    default_settings = '"_lightmapsamplesize" "24"\n"_lightmapscale" "2"\n'
    common = ['-fs_basegame', 'baseq3', '-fs_basepath', directory, '-fs_homepath', directory/'home']
    results, invalid = [], []
    left = box((-224,0,0),(-160,64,64))
    right = box((-160,0,0),(-96,64,64))

    def compile_case(label, text, game, threads, options):
        source.write_text(text, encoding='utf-8')
        arguments = ['-game', game, *common, '-threads', threads, *options, source]
        run(compiler, arguments, directory, label)
        return payloads(source.with_suffix('.bsp').read_bytes()), extras(source), arguments

    for style in PROJECTION:
        legacy = adapt(plain, style)
        # The same source spelling and argv are used for the previous/new comparison.
        legacy_lumps, _, args = compile_case(style+'-legacy', legacy, 'quake3', 1, ['-meta'])
        legacy_srf = source.with_suffix('.srf').read_bytes()
        if reference:
            run(reference, args, directory, style+'-reference')
            assert payloads(source.with_suffix('.bsp').read_bytes()) == legacy_lumps
            assert source.with_suffix('.srf').read_bytes() == legacy_srf

        # Annotate the existing floor to test explicit inheritance without changing geometry.
        floor = box((-272,-272,-16),(272,272,0))
        assert adapt(floor, style) in legacy
        zero = legacy.replace(adapt(floor, style), authored_brush(floor, style, [0]*6))
        inherited_lumps, _, _ = compile_case(style+'-inherit', zero, 'quake3', 1, ['-meta'])
        assert inherited_lumps == legacy_lumps, (style, 'explicit zero changed legacy BSP')
        assert source.with_suffix('.srf').read_bytes() == legacy_srf

        text = legacy.replace(marker, marker+default_settings+
            authored_brush(left, style, [0,0,0,0,0,8])+
            authored_brush(right, style, [0,0,0,0,0,32])+authored_patch(12))
        door = box((96,-48,16),(128,48,112))
        assert adapt(door, style) in text
        text = text.replace(adapt(door, style), authored_brush(door, style, [0,10,0,0,0,0]))
        for game in ('quake3', 'ja'):
            for meta in (False, True):
                options = ['-meta','-patchmeta'] if meta else []
                normalized = None
                for threads in (1,4):
                    label = f'{style}-{game}-'+('meta' if meta else 'ordinary')+f'-t{threads}'
                    lumps, srf, args = compile_case(label, text, game, threads, options)
                    authored = [(index, row) for index, row in srf.items() if row.get('authoredSampleSize', 0)]
                    assert {r['authoredSampleSize'] for _,r in authored} == {8,10,12,32}, (label, authored)
                    lit_authored = [(i,r) for i,r in authored if r['sampleSize'] != 0]
                    assert {r['authoredSampleSize'] for _,r in lit_authored} == {8,10,12,32}, (label, authored)
                    assert all(r['sampleSize'] == r['authoredSampleSize'] for _,r in lit_authored), (label, authored)
                    # -patchmeta retains a vertex-lit native patch for LOD/collision
                    # in addition to the lightmapped meta representation.
                    assert all(meta and struct.unpack_from('<i',lumps[13],i*(148 if game=='ja' else 104)+8)[0]==2
                               for i,r in authored if r['sampleSize']==0), (label,authored)
                    inherited = [row for row in srf.values() if not row.get('authoredSampleSize',0) and row.get('entity',0)==0]
                    # Shader spacing overrides the entity base, then entity scale
                    # applies exactly once through repeated classification/merging.
                    assert inherited and all(row['sampleSize']==64 for row in inherited), (label, inherited)
                    # Thread count is literal CLI provenance in the entity string.
                    semantic = [re.sub(rb'"_q3map2_cmdline" "[^"\n]*"', b'', lumps[0]), *lumps[1:]]
                    if normalized is None: normalized = semantic
                    else: assert semantic == normalized, (label, 'worker divergence')
                    record = {'case': label, 'surfaces':len(srf), 'authored_surfaces':len(authored),
                              'sizes':sorted({r['sampleSize'] for _,r in authored})}
                    if game == 'quake3' and style == 'quake':
                        base = ['-game',game,*common,'-threads',threads]
                        run(compiler,[*base,'-vis',source],directory,label+'-vis')
                        run(compiler,[*base,'-light','-fast',source],directory,label+'-light')
                        baked = payloads(source.with_suffix('.bsp').read_bytes())
                        assert len(baked[14]) > 0
                        charts = {}
                        for index,row in authored:
                            face, points = geometry_for_surface(baked,index)
                            size = row['authoredSampleSize']
                            if size in (8,32):
                                assert all(abs(p[2]-64)<0.01 for p in points), (label,size,points)
                                assert face[7]>=0
                                # q3map2 leaves legacy chart width/height fields zero;
                                # verify the actual packed UV footprint in the atlas.
                                uvs = [struct.unpack_from('<2f', baked[10], v*44+20)
                                       for v in range(face[3],face[3]+face[4])]
                                spans = [(max(uv[a] for uv in uvs)-min(uv[a] for uv in uvs))*128 for a in range(2)]
                                assert all(abs(span-64/size)<0.001 for span in spans), (label,size,spans)
                                charts.setdefault(size,[]).append(round((spans[0]+1)*(spans[1]+1)))
                        assert sum(charts[8]) > sum(charts[32]), (label,charts)
                        record['top_face_chart_texels'] = charts
                    results.append(record)

        # The compile minimum takes precedence even over an authored override.
        for minimum in (16,64):
            _, srf, _ = compile_case(style+f'-clamp-{minimum}',text,'quake3',1,['-meta','-minsamplesize',minimum])
            assert all(row['sampleSize']==max(minimum,row['authoredSampleSize']) for row in srf.values() if row.get('authoredSampleSize') and row['sampleSize'])
            assert {row['authoredSampleSize'] for row in srf.values() if row.get('authoredSampleSize')}=={8,10,12,32}, 'clamping must not erase distinct authored values through merging'

    # An override cannot turn a vertex-lit material into a lightmapped one.
    unlit = plain.replace(marker, marker+authored_brush(left,'quake',[0,0,0,0,0,8]).replace('q3mapx/stone','q3mapx/unlightmapped'))
    _, srf, _ = compile_case('unlightmapped',unlit,'quake3',1,['-meta'])
    authored = [row for row in srf.values() if row.get('authoredSampleSize')]
    assert authored and all(row['sampleSize']==0 and row['authoredSampleSize']==8 for row in authored)

    # Reject malformed versioned data before overwriting previous outputs.
    good = adapt(plain,'quake').replace(marker, marker+authored_brush(left,'quake',[0,0,0,0,0,8])+authored_patch(12))
    compile_case('malformed-carrier',good,'quake3',1,['-meta'])
    saved = {source.with_suffix(ext):source.with_suffix(ext).read_bytes() for ext in ('.bsp','.srf','.prt')}
    for ext in ('.lin','.reg'):
        saved[source.with_suffix(ext)] = ('previous '+ext).encode()
    broken = []
    for value in ('-1','+1','+-1','1.0','1e1','nan','inf','16385','9999999999999999999999','""','"8 9"'):
        broken.append(('face-'+str(len(broken)),good.replace('lightmapSampleSize 8','lightmapSampleSize '+value)))
        broken.append(('patch-'+str(len(broken)),good.replace('lightmapSampleSize 12','lightmapSampleSize '+value)))
    broken += [('face-missing',good.replace(' lightmapSampleSize 8','')),
               ('patch-missing',good.replace('lightmapSampleSize 12\n','')),
               ('face-version',good.replace('q3mapxBrushDef1','q3mapxBrushDef2')),
               ('patch-version',good.replace('q3mapxPatchDef1','q3mapxPatchDef2')),
               ('mixed-projection',good.replace('q3mapxBrushDef1 quake','q3mapxBrushDef1 valve220'))]
    for label,text in broken:
        source.write_text(text,encoding='utf-8')
        for destination,contents in saved.items(): destination.write_bytes(contents)
        command = [str(compiler),'-game','quake3',*map(str,common),'-meta',str(source)]
        result = subprocess.run(command,cwd=directory,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90)
        (directory/(label+'.log')).write_bytes(result.stdout)
        assert result.returncode==1, (label,result.returncode,result.stdout[-2500:])
        assert b'Invalid MAP' in result.stdout or b'Incomplete MAP' in result.stdout, (label,result.stdout[-2500:])
        for destination,contents in saved.items(): assert destination.read_bytes()==contents, (label,destination)
        assert source.read_text(encoding='utf-8')==text
        invalid.append(label)
    source.write_text(good,encoding='utf-8')
    report = {'matrix':results,'rejected':invalid,'reference_legacy_parity':bool(reference),
              'clamp_minima':[16,64], 'unlightmapped_preserved':True,
              'compiler_sha256':hashlib.sha256(compiler.read_bytes()).hexdigest()}
    (root/'results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',required=True,type=Path)
    p.add_argument('--reference',type=Path)
    p.add_argument('--work-dir',required=True,type=Path)
    a=p.parse_args()
    report=run_matrix(a.compiler.resolve(),a.work_dir.resolve(),a.reference.resolve() if a.reference else None)
    print(f"surface density: {len(report['matrix'])} native builds; {len(report['rejected'])} malformed inputs rejected")

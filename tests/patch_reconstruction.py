"""Archive-free triangle-to-patch inference, independent rebuilt triangles and guards.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from fixtures import create_fixture
from integration import run
from patch_input import payloads
from patch_paint import painted
from patch_source import archive, FOOTER, primitives
from patch_color_recovery import read_surfaces, pack_bsp


def triangles(data, policy):
    lumps=payloads(data); stride=148 if data[:4]==b'RBSP' else 104
    output=[]
    for s in read_surfaces(data):
        if s['shader']!='textures/q3mapx/paint' or s['kind']==2: continue
        first,count=struct.unpack_from('<2i',lumps[13],s['surface']*stride+20)
        indices=struct.unpack_from('<'+str(count)+'i',lumps[11],first*4)
        vertices=[]
        for xyz,uv,rgba in s['controls']:
            channels=rgba[:4] if policy=='rgba' else rgba[3:4] if policy=='alpha' else ()
            vertices.append((*tuple(a+b for a,b in zip(xyz,s['origin'])),*uv,*channels))
        for i in range(0,count,3):
            face=[vertices[j] for j in indices[i:i+3]]
            output.append(min(tuple(face[j:]+face[:j]) for j in range(3)))
    return sorted(output)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('compiler','work-dir'): p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args(); exe=a.compiler.resolve(); root=a.work_dir.resolve(); root.mkdir(parents=True,exist_ok=True)
    source=create_fixture(root,patch=False); plain=source.read_text(encoding='utf-8')
    shaders=source.parent.parent/'scripts/q3mapx_tests.shader'; base_shaders=shaders.read_text(encoding='utf-8')
    raven=source.parent.parent/'shaders'; raven.mkdir(exist_ok=True); (raven/'shaderlist.txt').write_text('q3mapx_tests\n',encoding='utf-8')
    def material(extra='surfaceparm nonsolid'):
        text=base_shaders+'\ntextures/q3mapx/paint\n{\n'+extra+'\n{ map $whiteimage rgbGen vertex alphaGen vertex }\n}\n'
        shaders.write_text(text,encoding='utf-8'); (raven/'q3mapx_tests.shader').write_text(text,encoding='utf-8')
    material(); cases=[]; guards=[]; carriers={}
    def recover(base,bsp,label,options=()):
        output=source.with_name(label+'.map')
        run(exe,[*base,'-decompile','-patch-recovery','fit',*options,'-o',output,bsp],root,label)
        return output,json.loads(Path(str(output)+'.recovery.json').read_text(encoding='utf-8'))
    for game in ('quake3','ja'):
        base=['-game',game,'-fs_basegame','baseq3','-fs_basepath',root,'-fs_homepath',root/'home','-threads',1]
        for curved in (False,True):
            for width in (3,5):
                for policy in ('rgba','alpha','none'):
                    label=f'{game}-{curved}-{width}-{policy}'
                    mode='material' if policy=='rgba' else 'lighting'
                    world=painted(mode=mode,subdivisions=8,size=7,curved=curved,width=width,extent=64)
                    door=painted(mode=mode,subdivisions=8,size=13,curved=curved,origin=(96,-48,144),extent=64)
                    text=plain.replace('"message" "q3mapx regression"\n','"message" "q3mapx regression"\n'+world)
                    text=text.replace('"targetname" "test_door"\n','"targetname" "test_door"\n'+door)
                    source.write_text(text,encoding='utf-8')
                    run(exe,[*base,'-mi',96,source],root,label+'-bsp')
                    run(exe,[*base,'-light','-fast',source],root,label+'-light')
                    raw=source.with_suffix('.bsp').read_bytes(); payload=archive(raw); old=raw[:-len(payload)-FOOTER]
                    bsp=source.with_name(label+'.bsp'); bsp.write_bytes(old)
                    bsp.with_suffix('.map').write_text('poisoned adjacent source',encoding='utf-8'); bsp.with_suffix('.srf').write_text('poisoned',encoding='utf-8')
                    output,report=recover(base,bsp,label+'-fit',['-patch-colors',policy,'-format',('map','map_bp','map_220')[len(cases)%3]])
                    fit=report['patch_recovery']['triangle_fitting']
                    assert fit['fitted_patches']==report['patches']==2,(label,fit)
                    assert not report['patch_recovery']['geometry_binding_verified'] and not fit['original_source_proven']
                    assert output.read_text(encoding='utf-8').count('q3mapxPatchDef2')==2
                    assert output.read_text(encoding='utf-8').count('lightmapSampleSize 0')==2
                    run(exe,[*base,output],root,label+'-rebuild')
                    rebuilt=output.with_suffix('.bsp').read_bytes()
                    assert triangles(rebuilt,policy)==triangles(old,policy),label+' reconstructed oriented triangle samples differ'
                    assert sum(s['kind']==2 for s in read_surfaces(rebuilt))==0,'Fitting must not add native collision patches'
                    run(exe,[*base,'-light','-fast',output],root,label+'-relight')
                    assert triangles(output.with_suffix('.bsp').read_bytes(),policy)==triangles(old,policy),label+' relighting changed requested samples'
                    auto,auto_report=recover(base,bsp,label+'-auto',['-patch-recovery','auto','-patch-colors',policy])
                    assert auto_report['patch_recovery']['triangle_fitting']['fitted_patches']==2
                    bsp.write_bytes(raw)
                    exact,exact_report=recover(base,bsp,label+'-source-auto',['-patch-recovery','auto'])
                    assert exact_report['patch_recovery']['restored_source_patches']==2 and exact_report['patch_recovery']['triangle_fitting']['fitted_patches']==0
                    assert primitives(exact.read_text(encoding='utf-8'))==primitives(text)
                    assert bsp.read_bytes()==raw
                    cases.append(dict(case=label,oriented_triangles=len(triangles(old,policy)),rebuild_and_relight_equal=True,
                        split_surface_count=sum(s['shader']=='textures/q3mapx/paint' for s in read_surfaces(old))))
                    if policy=='rgba': carriers[game]=(base,old,raw,text)

        # Genuine legacy patchDef2 compiled to triangle-only output, with no
        # q3mapx paint metadata at any stage. Auto must also fit a legacy patch
        # alongside archived painted sources without duplicating either.
        legacy=painted(curved=True,origin=(-64,96,96),extent=64)
        legacy=legacy.replace('q3mapxPatchDef2','patchDef2')
        legacy=re.sub(r'^(lightmapSampleSize|vertexRGB|paintSubdivisions).*\n','',legacy,flags=re.M)
        legacy=re.sub(r'\( ([^()]*) \)',lambda m:'( '+' '.join(m[1].split()[:5])+' )' if len(m[1].split())==9 else m[0],legacy)
        for mixed in (False,True):
            text=(carriers[game][3] if mixed else plain).replace('"message" "q3mapx regression"\n','"message" "q3mapx regression"\n'+legacy)
            source.write_text(text,encoding='utf-8')
            run(exe,[*base,'-meta','-patchmeta',source],root,game+f'-legacy-{mixed}-bsp')
            bsp=source.with_name(game+f'-legacy-{mixed}.bsp'); data=source.with_suffix('.bsp').read_bytes(); bsp.write_bytes(data)
            output,report=recover(base,bsp,game+f'-legacy-{mixed}-recover',['-patch-recovery','auto','-patch-colors','alpha'])
            fit=report['patch_recovery']['triangle_fitting']
            assert fit['fitted_patches']==1 and report['patch_recovery']['restored_source_patches']==(2 if mixed else 0),(game,mixed,fit)
            run(exe,[*base,output],root,game+f'-legacy-{mixed}-rebuild')
            assert triangles(data,'alpha')==triangles(output.with_suffix('.bsp').read_bytes(),'alpha')
            cases.append(dict(case=game+f'-legacy-{mixed}',oriented_triangles=len(triangles(data,'alpha')),rebuild_and_relight_equal=False,legacy_patchDef2=True,mixed_source_and_fit=mixed))

    for game,(base,old,raw,text) in carriers.items():
        stride,vs,co=(148,80,64) if game=='ja' else (104,44,40)
        paint=[s for s in read_surfaces(old) if s['shader']=='textures/q3mapx/paint' and s['model']==0]
        s=paint[0]['surface']; lumps=[bytearray(x) for x in payloads(old)]
        fv,_,fi,ni=struct.unpack_from('<4i',lumps[13],s*stride+12)
        variants=[]
        for label,kind in (('hole',0),('winding',1),('position',2),('uv',3),('alpha',4),('brush-material',5)):
            changed=[bytearray(x) for x in lumps]
            if kind==0: struct.pack_into('<i',changed[13],s*stride+24,ni-3)
            elif kind==1:
                indices=struct.unpack_from('<3i',changed[11],fi*4)
                struct.pack_into('<3i',changed[11],fi*4,indices[1],indices[0],indices[2])
            elif kind in (2,3):
                offset=fv*vs+(8 if kind==2 else 12)
                value=struct.unpack_from('<f',changed[10],offset)[0]; struct.pack_into('<f',changed[10],offset,value+0.123)
            elif kind==4: changed[10][fv*vs+co+3]^=127
            else:
                shader=struct.unpack_from('<i',changed[13],s*stride)[0]
                struct.pack_into('<i',changed[8],8,shader)
            variants.append((label,pack_bsp(old,changed)))
        if game=='ja':
            changed=[bytearray(x) for x in lumps]
            for surf in paint:
                start=surf['surface']*stride; changed[13][start+33]=1
                first,count=struct.unpack_from('<2i',changed[13],start+12)
                for i in range(first,first+count): changed[10][i*vs+co+7]=(changed[10][i*vs+co+3]+1)%256
            variants.append(('active-style',pack_bsp(old,changed)))
        for label,data in variants:
            bsp=source.with_name(game+'-'+label+'.bsp'); bsp.write_bytes(data)
            output,report=recover(base,bsp,game+'-'+label+'-guard',['-patch-colors','rgba'])
            fit=report['patch_recovery']['triangle_fitting']
            # BSP index ranges can be shared by models. A winding edit may
            # therefore invalidate both components, not only the edited owner.
            assert fit['fitted_patches']<2 and not any(d['model']==0 and d['status']=='fitted' for d in fit['decisions']),(game,label,fit)
            guards.append(dict(case=game+'-'+label,counts=fit['counts']))
        bsp=source.with_name(game+'-limits.bsp'); bsp.write_bytes(old)
        for label,options in (('budget',['-patch-fit-work',1]),('modifier',[]),('solid',[])):
            if label=='modifier': material('surfaceparm nonsolid\nq3map_tcMod translate 1 2')
            if label=='solid': material('')
            output,report=recover(base,bsp,game+'-'+label,['-patch-colors','rgba',*options])
            fit=report['patch_recovery']['triangle_fitting']; assert fit['fitted_patches']==0,(label,fit)
            if label=='budget': assert fit['work_used']<=1 and fit['counts']['work_limit']>0
            guards.append(dict(case=game+'-'+label,counts=fit['counts']))
        material()
        for label,options in (('bad-policy',['-patch-recovery','oops']),('bad-budget',['-patch-recovery','fit','-patch-fit-work',0]),
                              ('budget-without-fit',['-patch-fit-work',1]),('wrong-format',['-patch-recovery','fit','-format','obj'])):
            output=root/'preserved.map'; report=root/'preserved.json'; output.write_bytes(b'old map'); report.write_bytes(b'old report')
            result=subprocess.run([str(exe),*map(str,[*base,'-decompile',*options,'-o',output,'-report',report,bsp])],cwd=root,capture_output=True,timeout=60)
            assert result.returncode==1 and output.read_bytes()==b'old map' and report.read_bytes()==b'old report'
            guards.append(dict(case=game+'-'+label,outputs_preserved=True))
    (root/'validation.json').write_text(json.dumps(dict(cases=cases,guards=guards,compiler_sha256=hashlib.sha256(exe.read_bytes()).hexdigest()),indent=2)+'\n',encoding='utf-8')
    print(f'{len(cases)} archive-free reconstruction/rebuild cases and {len(guards)} guards passed')


if __name__=='__main__': main()

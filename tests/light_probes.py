"""Direct light recovery probes: analytic fields, material tracing and output preservation.
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

from fixtures import box, create_fixture, create_lighting_fixture
from integration import run
from lighting_evidence import parts, pack


def floor_surface(native,game):
    lumps=parts(native); ss=104 if game=='quake3' else 148; vs=44 if game=='quake3' else 80
    for offset in range(0,len(lumps[13]),ss):
        first,count=struct.unpack_from('<2i',lumps[13],offset+12)
        vertices=[struct.unpack_from('<3f',lumps[10],(first+i)*vs) for i in range(count)]
        if vertices and all(p[2]==0 for p in vertices): return offset//ss
    raise AssertionError('Missing floor surface')


def plain_scene(root,game):
    source=create_fixture(root,patch=False,shader_directory='scripts' if game=='quake3' else 'shaders')
    walls=[((-144,-144,-16),(144,144,0)),((-144,-144,256),(144,144,272)),
           ((-144,-144,0),(-128,144,256)),((128,-144,0),(144,144,256)),
           ((-128,-144,0),(128,-128,256)),((-128,128,0),(128,144,256))]
    source.write_text('{\n"classname" "worldspawn"\n"_ambient" "3"\n"_minlight" "2"\n'+''.join(box(a,b) for a,b in walls)+'}\n'+
                      '{\n"classname" "info_player_deathmatch"\n"origin" "0 0 32"\n}\n'+
                      '{\n"classname" "light"\n"origin" "-64 0 192"\n"light" "200"\n"_color" "0.4 0.8 1"\n}\n')
    return source


def contribution(light,point,normal,half_lambert=False):
    origin=light['origin']; delta=[a-b for a,b in zip(origin,point)]; distance=math.sqrt(sum(x*x for x in delta))
    direction=[d/distance for d in delta] if distance else [0,0,0]
    flags=light.get('spawnflags',0); angle=1 if flags&3 else max(0,sum(a*b for a,b in zip(normal,direction)))
    if light.get('target') is not None: angle=max(0,sum(a*b for a,b in zip(normal,direction)))
    if half_lambert and (not flags&3 or 'target' in light): angle=(min(1,angle)*.5+.5)**2 if angle>.001 else 0
    scale=light.get('angle_scale',0)
    if scale: angle=min(1,angle/scale)
    radius=math.sqrt(distance*distance+light.get('extra_distance',0)**2); radius=max(16,radius)
    intensity=light.get('intensity',300) or 300
    sign=-1 if intensity<0 else 1; photons=abs(intensity)*7500
    amount=max(0,angle*photons/8000-radius*(light.get('fade',1) or 1)) if flags&1 and 'target' not in light else photons*angle/(radius*radius)
    if 'target' in light:
        axis=[b-a for a,b in zip(origin,light['target'])]; length=math.sqrt(sum(x*x for x in axis)); axis=[x/length for x in axis]
        along=-sum(a*b for a,b in zip(delta,axis)); cone=((light.get('radius',64) or 64)+16)/(length or 64)*along
        radial=math.sqrt(sum((p-o-a*along)**2 for p,o,a in zip(point,origin,axis)))
        if along<0 or radial>=cone: amount=0
        else: amount*=min(1,(cone-radial)/32)
    color=light.get('color',[1,1,1]); maximum=max(color)
    if not flags&32 and maximum: color=[c/maximum for c in color]
    return [amount*c*sign for c in color]


def main():
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('--compiler',type=Path,required=True); p.add_argument('--work-dir',type=Path,required=True)
    a=p.parse_args(); exe=a.compiler.resolve(); root=a.work_dir.resolve(); root.mkdir(parents=True,exist_ok=True)
    records=[]; failures=[]; maximum=0

    def probe(folder,game,bsp,payload,label,options=()):
        request=folder/(label+'-request.json'); request.write_text(json.dumps(payload)); dest=folder/(label+'.json')
        before=bsp.read_bytes()
        args=['-game',game,'-fs_basepath',folder,'-fs_homepath',root/'home','-fs_basegame','baseq3',
              '-threads',4,'-light','-probes',request,'-probe-report',dest,*options,bsp]
        run(exe,args,folder,label,timeout=120)
        assert bsp.read_bytes()==before
        result=json.loads(dest.read_text()); records.append({'case':folder.name+'-'+label,'sources':result['active_sources'],'responses':result['nonzero_responses']})
        return result
    proposals=[{'origin':[0,0,128],'intensity':300,'color':[1,.25,0],'extra_distance':32},
               {'origin':[0,0,224],'intensity':400,'color':[.2,.4,1],'style':2,'target':[0,0,0],'radius':64},
               {'origin':[64,0,128],'intensity':-100},
               {'origin':[-64,-64,128],'intensity':500,'spawnflags':1,'fade':.5}]
    retained={'origin':[-64,0,192],'intensity':200,'color':[.4,.8,1]}
    for game in ('quake3','ja','qfusion'):
        folder=root/game; source=plain_scene(folder,game)
        base=['-game',game,'-fs_basepath',folder,'-fs_homepath',root/'home','-fs_basegame','baseq3']
        run(exe,[*base,'-threads',1,'-meta','-keeplights',source],folder,'bsp')
        run(exe,[*base,'-threads',1,'-vis',source],folder,'vis')
        bsp=source.with_suffix('.bsp'); native=bsp.read_bytes(); floor=floor_surface(native,game)
        samples=[{'surface':floor,'position':[x,y,1],'normal':[0,0,1]} for x in (-96,-32,0,32,96) for y in (-96,0,96)]
        samples.append({'surface':floor,'position':[0,0,-100],'normal':[0,0,1]})
        request=folder/'request.json'; request.write_text(json.dumps({'schema_version':1,'samples':samples,'lights':proposals}))
        # Poison legacy sidecars: a recovery diagnostic must use the BSP, never
        # hidden original light entities or stale surface extras from these files.
        source.write_text('not a map\n'); source.with_suffix('.srf').write_text('not surface extras\n')
        custom=source.parent.parent/('scripts' if game=='quake3' else 'shaders')/'q3map2_fixture.shader'; custom.write_text('// retained generated shader\n')
        protected={f:f.read_bytes() for f in (bsp,source,source.with_suffix('.srf'),custom,request)}
        expected=None
        for threads in (1,4):
            out=folder/f'probes-{threads}.json'
            run(exe,[*base,'-threads',threads,'-light','-probes',request,'-probe-report',out,'-q3','-nosRGBcolor','-lightanglehl',0,'-nofastpoint',bsp],folder,f'probe-{threads}',timeout=120)
            data=json.loads(out.read_text())
            assert all(f.read_bytes()==content for f,content in protected.items())
            assert data['source_sha256']==hashlib.sha256(native).hexdigest() and data['request_sha256']==hashlib.sha256(request.read_bytes()).hexdigest()
            assert data['world_ambient']==[3,3,3] and data['world_minlight']==[2,2,2]
            assert data['active_sources']==5 and data['nonzero_responses']>0
            assert data['nonzero_responses']==sum(len(s['responses']) for s in data['samples'])
            if expected is not None: assert data==expected
            expected=data
            for sample in data['samples'][:-1]:
                assert sample['status']=='sampled'
                actual={r['source']:r['linear_rgb'] for r in sample['responses']}
                for light in data['sources']:
                    model=retained if light['bsp_entity'] is not None else proposals[light['proposed_light']]
                    wanted=contribution(model,sample['probe_origin'],sample['normal']); got=actual.get(light['index'],[0,0,0])
                    error=max(abs(a-b) for a,b in zip(got,wanted)); maximum=max(maximum,error)
                    assert error<2e-4,(game,light,sample,got,wanted,error)
            assert data['samples'][-1]['status']=='outside_usable_cluster' and not data['samples'][-1]['responses']
            records.append({'case':game+f'-{threads}','sources':data['active_sources'],'responses':data['nonzero_responses']})

        # Restore a stripped retained light by proposing precisely that source.
        # This is a forward-equivalence check; its original position is not a
        # discovery oracle for any inverse solver.
        lump=parts(native); lump[0]=re.sub(rb'\{\s*"classname" "light"[^{}]*\}\s*',b'',lump[0])
        stripped=folder/'stripped.bsp'; stripped.write_bytes(pack(native,lump))
        settings=['-q3','-nosRGBcolor','-lightanglehl',0,'-nofastpoint']
        restored=probe(folder,game,stripped,{'schema_version':1,'samples':samples,'lights':[retained,*proposals]},'restored',settings)
        assert [s['direct_by_style'] for s in restored['samples']]==[s['direct_by_style'] for s in expected['samples']]
        assert all(s['bsp_entity'] is None for s in restored['sources'])
        tiny=probe(folder,game,stripped,{'schema_version':1,'samples':samples[:1],
                   'lights':[{'origin':[0,0,128],'intensity':2**-25}]},'tiny-proposal',settings)
        assert tiny['sources'][0]['photons']==2**-25*7500
        half=probe(folder,game,stripped,{'schema_version':1,'samples':samples[:15],'lights':proposals},'half-lambert',
                   [*settings,'-lightanglehl',1])
        assert half['settings']['half_lambert']
        for sample in half['samples']:
            actual={r['source']:r['linear_rgb'] for r in sample['responses']}
            for light in half['sources']:
                wanted=contribution(proposals[light['proposed_light']],sample['probe_origin'],sample['normal'],True)
                error=max(abs(a-b) for a,b in zip(actual.get(light['index'],[0,0,0]),wanted)); maximum=max(maximum,error)
                assert error<2e-4

        def fails(label,payload,needle,extra=(),destination=None):
            request=folder/(label+'-invalid.json'); request.write_bytes(payload if isinstance(payload,bytes) else json.dumps(payload).encode())
            out=destination or folder/'preserved.json'
            if out not in (request,bsp): out.write_text('preserved report\n')
            saved=out.read_bytes(); before=bsp.read_bytes()
            command=[exe,*base,'-threads',4,'-light','-probes',request,'-probe-report',out,*extra,bsp]
            result=subprocess.run(list(map(str,command)),cwd=folder,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
            log=result.stdout.decode(errors='replace'); (folder/(label+'-failure.log')).write_text(log)
            assert result.returncode==1 and needle in log,(label,result.returncode,log[-2000:])
            assert out.read_bytes()==saved and bsp.read_bytes()==before and not list(folder.glob('*.q3mapx-*'))
            failures.append(game+'-'+label)
        basic={'schema_version':1,'samples':samples[:1]}
        fails('unknown-field',{**basic,'guess':1},'Unknown or duplicate')
        fails('duplicate',b'{"schema_version":1,"schema_version":1,"samples":[]}', 'Unknown or duplicate')
        fails('nul',json.dumps(basic).encode()+b'\0garbage','Embedded NUL')
        fails('empty',{'schema_version':1,'samples':[]},'1..10000')
        fails('normal',{**basic,'samples':[{**samples[0],'normal':[0,0,0]}]},'nonzero')
        fails('surface',{**basic,'samples':[{**samples[0],'surface':200000}]},'surface index')
        fails('underflow',{**basic,'lights':[{'origin':[0,0,128],'intensity':1e-100}]},'not representable')
        fails('sun-target',{**basic,'lights':[{'origin':[0,0,128],'sun':True}]},'requires a target')
        fails('unsupported-options',basic,'does not support option',('-bounce',1))
        fails('pair-limit',{'schema_version':1,'samples':samples,'lights':proposals},'pair limit exceeded',('-probe-max-pairs',1))
        # All points see all of these sources; a worker must stop at the shared
        # response budget before opening the output report.
        fails('response-limit',{'schema_version':1,'samples':[samples[0]]*10000,
              'lights':[{'origin':[0,0,128],'intensity':300}]*11},'response limit exceeded',('-nofastpoint',))
        fails('report-request',basic,'separate .json output',destination=folder/'report-request-invalid.json')
        fails('report-bsp',basic,'separate .json output',destination=bsp)
        for key,value in (('_samples','1e2'),('style','1.0')):
            changed=parts(native)
            changed[0]=changed[0].replace(b'"classname" "light"',f'"classname" "light"\n"{key}" "{value}"'.encode())
            try:
                bsp.write_bytes(pack(native,changed))
                fails('native-'+key,basic,'Trailing BSP entity numeric data')
            finally: bsp.write_bytes(native)
        changed=parts(native)
        changed[0]=changed[0].replace(b'"classname" "light"',b'"classname" "light"\n"spawnflags" "+0000"\n"_samples" "01"')
        try:
            bsp.write_bytes(pack(native,changed))
            spelled=probe(folder,game,bsp,{'schema_version':1,'samples':samples,'lights':proposals},'integer-spelling',settings)
            assert [s['direct_by_style'] for s in spelled['samples']]==[s['direct_by_style'] for s in expected['samples']]
        finally: bsp.write_bytes(native)

    for mode in ('sun','emitter','mixed'):
        folder=root/mode; source=create_lighting_fixture(folder); text=source.read_text()
        if mode!='mixed':
            text=re.sub(r'\{\n"classname" "light"\n[^{}]*\}\n','',text)
            text=text.replace('q3mapx/emitter','q3mapx/stone') if mode=='sun' else text.replace('q3mapx/sky','q3mapx/stone')
        source.write_text(text)
        base=['-game','quake3','-fs_basepath',folder,'-fs_homepath',root/'home','-threads',1]
        run(exe,[*base,'-meta','-keeplights',source],folder,'bsp'); run(exe,[*base,'-vis',source],folder,'vis')
        bsp=source.with_suffix('.bsp'); floor=floor_surface(bsp.read_bytes(),'quake3')
        samples=[{'surface':floor,'position':[x,y,16],'normal':[0,0,1]}
                 for x in (-208,-176,-144,-112,-80,80,112,144,176,208) for y in (64,96,128,160,192)]
        payload={'schema_version':1,'samples':samples}
        baseline=probe(folder,'quake3',bsp,payload,'baseline')
        assert baseline['nonzero_responses']>0
        if mode!='mixed': assert all(s['surface'] is not None and s['bsp_entity'] is None for s in baseline['sources'])
        if mode=='sun': assert all(s['type']=='sun_sky' for s in baseline['sources'])
        if mode=='emitter': assert all(s['type']!='sun_sky' for s in baseline['sources'])
        if mode=='emitter':
            no_splash=probe(folder,'quake3',bsp,payload,'no-backsplash',['-backsplash',0,-999])
            assert no_splash['generated_sources']<baseline['generated_sources']
        if mode=='mixed':
            assert {s['type'] for s in baseline['sources']}=={'point','area','sun_sky'}
            unshadowed=probe(folder,'quake3',bsp,payload,'unshadowed',['-notrace'])
            assert unshadowed['samples']!=baseline['samples']
            for texture,channels in (('fence',(3,)),('filter',(0,1,2))):
                image=source.parent.parent/f'textures/q3mapx/{texture}.tga'; saved=image.read_bytes(); changed=bytearray(saved)
                for offset in range(18,len(changed),4):
                    for channel in channels: changed[offset+channel]=0 if texture=='fence' else 255
                try:
                    image.write_bytes(changed); modified=probe(folder,'quake3',bsp,payload,'changed-'+texture)
                    assert modified['samples']!=baseline['samples'],texture
                finally: image.write_bytes(saved)
            sun=probe(folder,'quake3',bsp,{**payload,'lights':[{'origin':[0,0,224],'target':[0,0,0],'sun':True,'intensity':30}]},'proposed-sun')
            proposed=[s for s in sun['sources'] if s['proposed_light'] is not None]
            assert len(proposed)==1 and proposed[0]['type']=='sun_sky' and proposed[0]['surface'] is None
            assert any(r['source']==proposed[0]['index'] for s in sun['samples'] for r in s['responses'])
    (root/'results.json').write_text(json.dumps({'cases':records,'preserved_outputs':failures,'max_absolute_component_error':maximum},indent=2)+'\n')
    print(f'{len(records)} probe reports and {len(failures)} preserved failures passed; maximum analytic RGB component error {maximum:.6g}')


if __name__=='__main__': main()

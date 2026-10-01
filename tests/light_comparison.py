"""Native baked-texel comparisons, independent transfer arithmetic and failure bounds.
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

from fixtures import create_fixture, create_lighting_fixture
from integration import run
from light_probes import color_space_options, plain_scene
from lighting_evidence import parts, pack


def encode(color, settings, brightness):
    # Independent scalar expression, using the report's declared parameters.
    c=[max(0,settings['contrast_factor']*(v-128)+128) for v in color]
    if settings['gamma']!=1: c=[255*(v/255)**(1/settings['gamma']) for v in c]
    if settings['exposure'] and max(c)>0:
        scale=255*(-math.expm1(-max(c)/settings['exposure']))/max(c)
        c=[v*scale for v in c]
    if settings['saturation']!=1:
        gray=sum(a*b for a,b in zip(c,(.3086,.6094,.0820)))
        c=[max(0,gray*(1-settings['saturation'])+v*settings['saturation']) for v in c]
    c=[v*(brightness if brightness>0 else 1)/settings['compensation'] for v in c]
    if max(c)>settings['maximum_light']:
        c=[v*settings['maximum_light']/max(c) for v in c]
    if settings['lightmaps_srgb']:
        c=[math.floor(255*(12.92*(v/255) if v/255<.0031308 else 1.055*(v/255)**(1/2.4)-.055)+.5) for v in c]
    return c


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,required=True); p.add_argument('--work-dir',type=Path,required=True)
    a=p.parse_args(); exe=a.compiler.resolve(); root=a.work_dir.resolve(); root.mkdir(parents=True,exist_ok=True)
    records=[]; failures=[]; maximum_transfer_error=0
    common=['-q3','-gamma',1,'-compensate',1,'-lightanglehl',0,'-nofastpoint']
    payload={'schema_version':1,'baked_lightmaps':{'stride':1,'normal_offset':1}}

    def compare(folder,game,bsp,label,request=payload,options=(),threads=4,oracle=True):
        nonlocal maximum_transfer_error
        source=bsp.read_bytes(); request_path=folder/(label+'-request.json'); request_path.write_text(json.dumps(request))
        dest=folder/(label+'.json')
        base=['-game',game,'-fs_basepath',folder,'-fs_homepath',root/'home','-fs_basegame','baseq3','-threads',threads]
        run(exe,[*base,'-light','-probes',request_path,'-probe-report',dest,*common,*color_space_options(options),bsp],folder,label,timeout=120)
        assert bsp.read_bytes()==source
        data=json.loads(dest.read_text()); size=data['baked_comparison']['page_size']; native=parts(source)
        assert data['settings']['lightmaps_srgb']==('-sRGBlight' in options)
        assert data['source_sha256']==hashlib.sha256(source).hexdigest()
        assert not data['light_inference_performed'] and not data['baked_comparison']['encoding_calibrated']
        assert data['baked_comparison']['selected_samples']==len(data['samples'])
        errors=[]; without255=[]; unknown=0
        for sample in data['samples']:
            obs=sample['baked_lightmap']; x,y=obs['texel']; offset=((obs['page']*size+y)*size+x)*3
            actual=list(native[14][offset:offset+3]); assert obs['observed_rgb']==actual
            assert obs['observed_has_255_channel']==(255 in actual)
            if 'predicted_rgb' not in obs:
                unknown+=1; assert obs['status'] in ('unknown_trace','unrepresentable_encoding'); continue
            residual=[a-b for a,b in zip(obs['predicted_rgb'],actual)]
            assert obs['residual_bytes']==residual
            errors.extend(residual)
            if 255 not in actual: without255.extend(residual)
            assert obs['predicted_rgb']==[int(v) for v in obs['encoded_before_byte_conversion']]
            if oracle:
                expected=encode(obs['hypothesis_linear_rgb'],data['settings'],obs['material_lightmap_brightness'])
                error=max(abs(a-b) for a,b in zip(expected,obs['encoded_before_byte_conversion']))
                maximum_transfer_error=max(maximum_transfer_error,error)
                assert error<2e-3,(label,obs,expected,error)
        for key,values in (('all_compared',errors),('without_observed_255_channel',without255)):
            summary=data['comparison_summary'][key]; assert summary['components']==len(values)
            if values:
                assert abs(summary['mae_bytes']-sum(map(abs,values))/len(values))<1e-12
                assert abs(summary['rmse_bytes']-math.sqrt(sum(v*v for v in values)/len(values)))<1e-12
                assert summary['maximum_error_bytes']==max(map(abs,values))
            else: assert summary['mae_bytes'] is None and summary['rmse_bytes'] is None and summary['maximum_error_bytes'] is None
        assert len(errors)//3+unknown==len(data['samples'])
        records.append({'case':folder.name+'-'+label,'selected':len(data['samples']),
                        'errors':data['comparison_summary']['all_compared'],'unknown':unknown})
        return data

    truth={'origin':[-64,0,192],'intensity':200,'color':[.4,.8,1]}
    modes={'linear':[], 'gamma':['-gamma',2.2,'-compensate',2],
           'srgb':['-sRGBlight'],
           'transfer':['-exposure',90,'-contrast',20,'-saturation',.7,'-brightness',1.4],
           'clipped':['-pointscale',10]}
    for game in ('quake3','ja','qfusion'):
        folder=root/game; source=plain_scene(folder,game)
        base=['-game',game,'-fs_basepath',folder,'-fs_homepath',root/'home','-fs_basegame','baseq3','-threads',4]
        run(exe,[*base,'-meta','-keeplights',source],folder,'bsp')
        run(exe,[*base,'-vis',source],folder,'vis')
        bsp=source.with_suffix('.bsp'); unlit=bsp.read_bytes()
        for mode,options in modes.items():
            bsp.write_bytes(unlit)
            run(exe,[*base,'-light',*common,*color_space_options(options),source],folder,'bake-'+mode,timeout=120)
            result=compare(folder,game,bsp,mode,options=options)
            assert result['comparison_summary']['all_compared']['samples']>100
            assert result['comparison_summary']['all_compared']['maximum_error_bytes']==0,(game,mode,result['comparison_summary'])
            if mode=='linear':
                lit=bsp.read_bytes(); baseline=result
                assert result==compare(folder,game,bsp,'linear-one',threads=1)
            if mode in ('gamma','transfer'):
                wrong=compare(folder,game,bsp,mode+'-wrong-encoding')
                assert wrong['comparison_summary']['all_compared']['mae_bytes']>1
        bsp.write_bytes(lit)
        # Exact ceilings must pass; selection keeps the original associations
        # while the extraction budget still covers the complete scene.
        metadata=baseline['baked_comparison']
        exact_limits={'max_samples':len(baseline['samples']),
                      'max_observations':metadata['evidence_observations'],
                      'max_work':metadata['evidence_work']}
        exact=compare(folder,game,bsp,'exact-budgets',{'schema_version':1,
            'baked_lightmaps':{**payload['baked_lightmaps'],**exact_limits}})
        assert exact['samples']==baseline['samples']
        chosen=baseline['samples'][0]['surface']
        selected=compare(folder,game,bsp,'surface-selector',{'schema_version':1,
            'baked_lightmaps':{**payload['baked_lightmaps'],'surfaces':[chosen]}})
        expected=[{**s,'index':i} for i,s in enumerate(s for s in baseline['samples'] if s['surface']==chosen)]
        assert selected['samples']==expected and 0<len(expected)<len(baseline['samples'])
        assert selected['baked_comparison']['evidence_work']==metadata['evidence_work']
        assert selected['baked_comparison']['evidence_observations']==metadata['evidence_observations']
        # The comparison must use the BSP and current assets, not hidden MAP/SRF.
        source.write_text('not a map\n'); source.with_suffix('.srf').write_text('not surface extras\n')
        shader=source.parent.parent/('shaders' if game=='ja' else 'scripts')/'q3map2_fixture.shader'
        shader.write_text('// preserved generated shader\n')
        protected={path:path.read_bytes() for path in (source,source.with_suffix('.srf'),shader)}
        assert compare(folder,game,bsp,'poisoned-sidecars')['samples']==baseline['samples']
        assert all(path.read_bytes()==data for path,data in protected.items())
        native=parts(lit); native[0]=re.sub(rb'\{\s*"classname" "light"[^{}]*\}\s*',b'',native[0])
        stripped=folder/'stripped.bsp'; stripped.write_bytes(pack(lit,native))
        missing=compare(folder,game,stripped,'stripped'); assert missing['comparison_summary']['all_compared']['mae_bytes']>5
        restored=compare(folder,game,stripped,'restored',{**payload,'lights':[truth]})
        assert restored['comparison_summary']==baseline['comparison_summary']
        misplaced=compare(folder,game,stripped,'misplaced',{**payload,'lights':[{**truth,'origin':[0,0,128]}]})
        assert misplaced['comparison_summary']['all_compared']['mae_bytes']>5
        inside=compare(folder,game,bsp,'inside-solid',{'schema_version':1,'baked_lightmaps':{'stride':4,'normal_offset':-1}})
        assert inside['comparison_summary']['unknown_trace']==len(inside['samples'])>0
        invalid=compare(folder,game,bsp,'encoding-overflow',options=['-gamma',.001,'-pointscale',1e6],oracle=False)
        assert invalid['comparison_summary']['unrepresentable_encoding']==len(invalid['samples'])>0

        def fail(label,request,needle,options=()):
            path=folder/(label+'-invalid.json'); path.write_text(json.dumps(request)); output=folder/'preserved.json'; output.write_text('preserved\n')
            before=bsp.read_bytes()
            command=[exe,*base,'-light','-probes',path,'-probe-report',output,*common,*color_space_options(options),bsp]
            result=subprocess.run(list(map(str,command)),cwd=folder,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
            log=result.stdout.decode(errors='replace'); (folder/(label+'-failure.log')).write_text(log)
            assert result.returncode==1 and needle in log,(label,result.returncode,log[-2000:])
            assert bsp.read_bytes()==before and output.read_text()=='preserved\n' and not list(folder.glob('*.q3mapx-*'))
            failures.append(game+'-'+label)
        fail('both',dict(payload,samples=[]),'exactly one')
        fail('offset',{'schema_version':1,'baked_lightmaps':{}},'normal_offset')
        for key,value,needle in (('stride',0,'outside supported range'),('max_samples',1,'sample limit exceeded'),
                                 ('max_observations',1,'observation limit exceeded'),('max_work',1,'work budget exceeded'),
                                 ('surfaces',[0,0],'Duplicate'),('surfaces',[199999],'outside the BSP')):
            fail(key+'-'+str(value),{'schema_version':1,'baked_lightmaps':{**payload['baked_lightmaps'],key:value}},needle)
        for key,needle in (('max_samples','sample limit exceeded'),('max_observations','observation limit exceeded'),
                           ('max_work','work budget exceeded')):
            fail(key+'-one-below',{'schema_version':1,
                'baked_lightmaps':{**payload['baked_lightmaps'],key:exact_limits[key]-1}},needle)
        vs=44 if game=='quake3' else 80; normal_offset=28 if game=='quake3' else 52
        for mode in ('absent','constant','zero-normal','nonfinite'):
            changed=parts(lit)
            if mode=='absent': changed[14]=b''
            else:
                vertices=bytearray(changed[10])
                for offset in range(0,len(vertices),vs):
                    if mode=='constant': struct.pack_into('<2f',vertices,offset+20,.5/(512 if game=='qfusion' else 128),.5/(512 if game=='qfusion' else 128))
                    if mode=='zero-normal': struct.pack_into('<3f',vertices,offset+normal_offset,0,0,0)
                    if mode=='nonfinite': struct.pack_into('<f',vertices,offset+20,float('nan'))
                changed[10]=bytes(vertices)
            try:
                bsp.write_bytes(pack(lit,changed))
                if mode=='nonfinite': fail(mode,payload,'active lightmap coordinate',('-force',))
                else:
                    empty=compare(folder,game,bsp,mode)
                    assert not empty['samples'] and empty['baked_comparison']['status']=='no_usable_observations'
                    if mode=='constant':
                        coarse=compare(folder,game,bsp,'constant-stride',{'schema_version':1,
                            'baked_lightmaps':{'stride':4,'normal_offset':1}})
                        assert not coarse['samples']
                        # Every primitive is excluded, including those skipped
                        # when extraction strides the representative records.
                        # Surfaces may reuse the same native index-table span.
                        surface_size=104 if game=='quake3' else 148
                        triangles=sum(struct.unpack_from('<i',changed[13],i+24)[0]//3
                                      for i in range(0,len(changed[13]),surface_size))
                        for report in (empty,coarse):
                            assert report['baked_comparison']['exclusions']['constant_regions']==triangles
            finally: bsp.write_bytes(lit)

    # Native four-style formats: a second styled spotlight must not receive
    # ambient or minlight merely because its atlas is present.
    for game in ('ja','qfusion'):
        folder=root/(game+'-styles'); source=plain_scene(folder,game)
        source.write_text(source.read_text()+
            '{\n"classname" "light"\n"origin" "0 0 224"\n"light" "320"\n"_color" "0.2 0.4 1"\n"style" "7"\n"target" "spot_aim"\n"radius" "180"\n}\n'+
            '{\n"classname" "info_null"\n"origin" "0 0 0"\n"targetname" "spot_aim"\n}\n')
        base=['-game',game,'-fs_basepath',folder,'-fs_homepath',root/'home','-fs_basegame','baseq3','-threads',4]
        run(exe,[*base,'-meta','-keeplights',source],folder,'bsp'); run(exe,[*base,'-vis',source],folder,'vis')
        run(exe,[*base,'-light',*common,*color_space_options(),source],folder,'bake',timeout=120)
        bsp=source.with_suffix('.bsp'); result=compare(folder,game,bsp,'styled-spot')
        assert {row['style'] for row in result['comparison_summary']['by_style']}=={0,7}
        assert result['comparison_summary']['all_compared']['maximum_error_bytes']==0
        assert any(s['baked_lightmap']['slot']>0 for s in result['samples'])
        for sample in result['samples']:
            obs=sample['baked_lightmap']
            if obs['slot']>0:
                direct=next((r['linear_rgb'] for r in sample['direct_by_style'] if r['style']==obs['style']),[0,0,0])
                assert direct==obs['hypothesis_linear_rgb']

    # Real curved/inline geometry and material emitters retain the same sampling
    # associations as extraction, but do not claim complete bake equivalence.
    for mode in ('curves-model','sun','emitter','mixed'):
        folder=root/mode; source=create_fixture(folder) if mode=='curves-model' else create_lighting_fixture(folder)
        if mode in ('sun','emitter'):
            text=re.sub(r'\{\n"classname" "light"\n[^{}]*\}\n','',source.read_text())
            source.write_text(text.replace('q3mapx/emitter','q3mapx/stone') if mode=='sun' else text.replace('q3mapx/sky','q3mapx/stone'))
        base=['-game','quake3','-fs_basepath',folder,'-fs_homepath',root/'home','-threads',4]
        run(exe,[*base,'-meta','-keeplights',source],folder,'bsp'); run(exe,[*base,'-vis',source],folder,'vis')
        run(exe,[*base,'-light',*common,*color_space_options(),source],folder,'bake',timeout=180)
        bsp=source.with_suffix('.bsp')
        result=compare(folder,'quake3',bsp,'comparison',{'schema_version':1,'baked_lightmaps':{'stride':2,'normal_offset':1}})
        assert result['comparison_summary']['all_compared']['samples']>0
        assert any(s['baked_lightmap']['geometry']=='stored_bezier' for s in result['samples'])
        if mode in ('sun','emitter'): assert all(s['bsp_entity'] is None for s in result['sources'])
        native=parts(bsp.read_bytes()); first,count=struct.unpack_from('<2i',native[7],40+24)
        assert any(first<=s['surface']<first+count for s in result['samples'])
        evidence_path=folder/'evidence.json'
        run(exe,[*base,'-bsp-evidence','-lighting','-lighting-stride',2,'-report',evidence_path,bsp],folder,'evidence')
        surfaces=json.loads(evidence_path.read_text())['baked_lighting']['surfaces']
        origins={0:[0,0,0],1:[112,0,64]}
        for sample in result['samples']:
            surface=surfaces[sample['surface']]; b=sample['baked_lightmap']; slot=surface['lightmap_slots'][b['slot']]
            points=slot['patch_observations'] if b['geometry']=='stored_bezier' else slot['observations']
            point=next(p for p in points if p['texel']==b['texel'])
            assert max(abs(p+o-v) for p,o,v in zip(point['position'],origins[surface['model']],sample['position']))<1e-4
        if mode=='mixed':
            # Bounce is deliberately not explained by this direct hypothesis.
            run(exe,[*base,'-light',*common,*color_space_options(),'-bounce',1,source],folder,'bake-bounce',timeout=180)
            bounced=compare(folder,'quake3',bsp,'bounced',{'schema_version':1,'baked_lightmaps':{'stride':2,'normal_offset':1}})
            assert bounced['comparison_summary']['all_compared']['mae_bytes']>result['comparison_summary']['all_compared']['mae_bytes']

    (root/'results.json').write_text(json.dumps({'cases':records,'preserved_outputs':failures,'max_transfer_component_error':maximum_transfer_error},indent=2)+'\n')
    print(f'{len(records)} baked comparison reports and {len(failures)} preserved failures passed; transfer error {maximum_transfer_error:.6g}')


if __name__=='__main__': main()

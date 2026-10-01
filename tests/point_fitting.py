"""Blind native point-light fitting, held-out controls and protected failures.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import time

from fixtures import box, create_lighting_fixture
from integration import run
from light_probes import color_space_options, plain_scene
from lighting_evidence import parts, pack


def entity(light):
    text=('{\n"classname" "light"\n"origin" "'+ ' '.join(map(str,light['origin']))+
            '"\n"light" "'+str(light['intensity'])+'"\n"_color" "'+
            ' '.join(map(str,light['color']))+'"\n')
    for field,key in (('style','style'),('spawnflags','spawnflags'),('extra_distance','_extradist')):
        if field in light: text+='"'+key+'" "'+str(light[field])+'"\n'
    return text+'}\n'


def strip_lights(native):
    lumps=parts(native)
    lumps[0]=re.sub(rb'\{\s*"classname" "light"[^{}]*\}\s*',b'',lumps[0])
    return pack(native,lumps)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,required=True); p.add_argument('--work-dir',type=Path,required=True)
    p.add_argument('--material-only',action='store_true',help='Quick explicit missing-light-image/fallback control')
    a=p.parse_args(); exe=a.compiler.resolve(); root=a.work_dir.resolve(); root.mkdir(parents=True,exist_ok=True)
    common=['-q3','-gamma',1,'-compensate',1,'-lightanglehl',0,'-nofastpoint']
    payload={'schema_version':1,'baked_lightmaps':{'stride':2,'normal_offset':1},
             'fit_point_lights':{'grid_spacing':96,'max_lights':1,'refinement_steps':7,'max_work':200000000}}
    cases=[]; failures=[]

    def compare(folder,game,bsp,label,request=payload,settings=(),threads=4):
        source=bsp.read_bytes(); path=folder/(label+'-request.json'); path.write_text(json.dumps(request))
        dest=folder/(label+'.json'); started=time.perf_counter()
        base=['-game',game,'-fs_basepath',folder,'-fs_homepath',root/'home','-fs_basegame','baseq3','-threads',threads]
        run(exe,[*base,'-light','-probes',path,'-probe-report',dest,*common,*color_space_options(settings),bsp],folder,label,timeout=500)
        assert bsp.read_bytes()==source and not list(folder.glob('*.q3mapx-*'))
        report=json.loads(dest.read_text()); fit=report['point_fit']
        assert report['settings']['lightmaps_srgb']==('-sRGBlight' in settings)
        assert report['settings']['entity_colors_srgb']==('-sRGBcolor' in settings)
        assert report['source_sha256']==hashlib.sha256(source).hexdigest()
        assert not fit['encoding_calibrated'] and not fit['original_bake_settings_known']
        by_index={s['index']:s for s in report['samples']}; errors=[[],[]]; blocks={}
        for observation in fit['validation_observations']:
            sample=by_index[observation['sample']]; baked=sample['baked_lightmap']
            key=(baked['page'],*(v//fit['atlas_block_size'] for v in baked['texel']))
            assert blocks.setdefault(key,observation['withheld'])==observation['withheld']
            assert 255 not in baked['observed_rgb']
            if 'trial_rgb' in observation:
                errors[observation['withheld']].extend(a-b for a,b in zip(observation['trial_rgb'],baked['observed_rgb']))
        if any(errors):
            for split,values in zip(('training','withheld'),errors):
                score=fit[split]['trial']; assert score['samples']*3==len(values)
                assert abs(score['rmse_bytes']-math.sqrt(sum(v*v for v in values)/len(values)))<1e-12
                assert abs(score['mae_bytes']-sum(map(abs,values))/len(values))<1e-12
                assert score['maximum_error_bytes']==max(map(abs,values))
        cases.append({'case':folder.name+'-'+label,'status':fit['status'],'accepted':fit['accepted'],
                      'trial':fit['best_trial'],'training':fit['training'],'withheld':fit['withheld'],
                      'work':fit['work_used'],'seconds':time.perf_counter()-started})
        return report

    def rebake(folder,game,bsp,report,settings=()):
        fit=report['point_fit']; native=bsp.read_bytes()
        output=folder/'baseq3/maps/reconstructed.map'
        base=['-game',game,'-fs_basepath',folder,'-fs_homepath',root/'home','-fs_basegame','baseq3','-threads',4]
        run(exe,[*base,'-decompile','-brush-order','rebuild','-o',output,bsp],folder,'recover-geometry')
        output.write_text(output.read_text()+''.join(entity(light) for light in fit['best_trial']))
        run(exe,[*base,'-meta','-keeplights',output],folder,'rebuild-bsp')
        run(exe,[*base,'-vis',output],folder,'rebuild-vis')
        run(exe,[*base,'-light',*common,*color_space_options(settings),output],folder,'rebuild',timeout=120)
        evidence=folder/'reconstructed-evidence.json'
        run(exe,[*base,'-bsp-evidence','-lighting','-lighting-stride',1,'-report',evidence,output.with_suffix('.bsp')],folder,'rebuild-evidence')
        key=lambda position,normal,style:(*(round(v,3) for v in position),*(round(v,3) for v in normal),style)
        values={}
        for surface in json.loads(evidence.read_text())['baked_lighting']['surfaces']:
            for slot in surface['lightmap_slots']:
                for sample in slot['observations']:
                    if sample['ambiguous_mapping']: continue
                    location=key(sample['position'],sample['normal'],slot['style'])
                    assert values.setdefault(location,sample['rgb'])==sample['rgb']
        errors=[]; transport=[]
        by_index={s['index']:s for s in report['samples']}
        for observation in fit['validation_observations']:
            sample=by_index[observation['sample']]; baked=sample['baked_lightmap']
            actual=values[key(sample['position'],sample['normal'],baked['style'])]
            errors.extend(a-b for a,b in zip(actual,baked['observed_rgb']))
            transport.extend(a-b for a,b in zip(actual,observation['trial_rgb']))
        assert max(map(abs,transport))<=1
        rmse=math.sqrt(sum(v*v for v in errors)/len(errors)); assert rmse<2
        cases[-1]['rebake_rmse']=rmse; cases[-1]['rebake_maximum_transport_difference']=max(map(abs,transport))
        assert bsp.read_bytes()==native

    def fail(folder,game,bsp,label,request,needle,settings=()):
        before=bsp.read_bytes(); path=folder/(label+'-invalid.json'); path.write_text(json.dumps(request))
        dest=folder/'preserved.json'; dest.write_text('preserved\n')
        command=[exe,'-game',game,'-fs_basepath',folder,'-fs_homepath',root/'home','-fs_basegame','baseq3',
                 '-threads',4,'-light','-probes',path,'-probe-report',dest,*common,*color_space_options(settings),bsp]
        process=subprocess.run(list(map(str,command)),cwd=folder,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=500)
        log=process.stdout.decode(errors='replace'); (folder/(label+'-failure.log')).write_text(log)
        assert process.returncode==1 and needle in log,(label,process.returncode,log[-1500:])
        assert bsp.read_bytes()==before and dest.read_text()=='preserved\n' and not list(folder.glob('*.q3mapx-*'))
        failures.append(folder.name+'-'+label)

    truth={'origin':[-73,21,177],'intensity':210,'color':[.35,.65,1]}
    for game,mode,settings in [('quake3','linear',[]),('ja','gamma',['-gamma',2.2,'-compensate',2]),
                               ('qfusion','srgb',['-sRGBlight']),('ja','styled',[]),
                               ('quake3','transfer',['-exposure',90,'-contrast',20,'-saturation',.7,'-brightness',1.4])]:
        folder=root/(game+'-'+mode); source=plain_scene(folder,game)
        request={**payload,'fit_point_lights':{**payload['fit_point_lights'],'style':7}} if mode=='styled' else payload
        original_light={**truth,'style':7} if mode=='styled' else truth
        source.write_text(re.sub(r'\{\s*"classname" "light"[^{}]*\}\s*','',source.read_text())+entity(original_light))
        base=['-game',game,'-fs_basepath',folder,'-fs_homepath',root/'home','-fs_basegame','baseq3','-threads',4]
        run(exe,[*base,'-meta','-keeplights',source],folder,'bsp'); run(exe,[*base,'-vis',source],folder,'vis')
        run(exe,[*base,'-light',*common,*color_space_options(settings),source],folder,'bake')
        bsp=source.with_suffix('.bsp'); native=bsp.read_bytes()
        retained=compare(folder,game,bsp,'retained',request,settings)
        assert not retained['point_fit']['accepted'] and retained['point_fit']['status']=='baseline_explains_observations'
        if a.material_only:
            texture=folder/'baseq3/textures/q3mapx/stone.tga'
            texture.write_bytes((texture.parent/'checker.tga').read_bytes())
            shader=folder/'baseq3/scripts/q3mapx_tests.shader'; script=shader.read_text()
            shader.write_text(script.replace('{','{\n    q3map_lightimage textures/q3mapx/unavailable.tga',1))
            try:
                for allowed in (False,True):
                    control={**payload,'fit_point_lights':{**payload['fit_point_lights'],'allow_implicit_materials':allowed}}
                    missing=compare(folder,game,bsp,'missing-material-'+str(allowed),control)['point_fit']
                    assert missing['status']=='unresolved_materials' and not missing['best_trial']
                    assert missing['exclusions']['missing_requested_light_image']==1
            finally: shader.write_text(script)
            (root/'results.json').write_text(json.dumps({'cases':cases,'preserved_outputs':failures},indent=2)+'\n')
            print('Explicit missing-light-image control passed with fallback present and both implicit-material policies')
            return
        bsp.write_bytes(strip_lights(native))
        source.write_text('hidden original map unavailable\n'); source.with_suffix('.srf').write_text('hidden surface extras unavailable\n')
        before={path:path.read_bytes() for path in (source,source.with_suffix('.srf'))}
        result=compare(folder,game,bsp,'blind',request,settings)
        assert all(path.read_bytes()==value for path,value in before.items())
        fit=result['point_fit']; assert fit['accepted'] and len(fit['best_trial'])==1
        recovered=fit['best_trial'][0]
        distance=math.dist(truth['origin'],recovered['origin']); assert distance<3,(distance,recovered)
        assert abs(recovered['intensity']/truth['intensity']-1)<.05
        assert max(abs(a-b) for a,b in zip(truth['color'],recovered['color']))<.04
        assert fit['withheld']['trial']['rmse_bytes']<1
        cases[-1]['position_error']=distance
        rebake(folder,game,bsp,result,settings)
        if mode!='linear': continue
        serial=compare(folder,game,bsp,'blind-one',settings=settings,threads=1)
        assert serial==result
        exact={**payload,'fit_point_lights':{**payload['fit_point_lights'],'max_work':fit['work_used']}}
        same=compare(folder,game,bsp,'exact-work',exact)
        assert same['point_fit']['best_trial']==fit['best_trial'] and same['point_fit']['work_used']==fit['work_used']
        fail(folder,game,bsp,'work-one-below',{**exact,'fit_point_lights':{**exact['fit_point_lights'],'max_work':fit['work_used']-1}},'work budget exceeded')
        for key,value,needle in (('grid_spacing',0,'outside supported range'),('max_candidates',1,'candidate grid exceeds'),
                                ('max_work',1,'work budget exceeded'),('max_lights',0,'outside supported range'),
                                ('mins',[0,0,0],'both mins and maxs'),('allow_implicit_materials',1,'must be boolean')):
            fail(folder,game,bsp,key,{**payload,'fit_point_lights':{**payload['fit_point_lights'],key:value}},needle)
        fail(folder,game,bsp,'explicit',{'schema_version':1,'samples':[{'surface':0,'position':[0,0,0],'normal':[0,0,1]}],
                                       'fit_point_lights':{}},'requires baked_lightmaps')
        # Change only withheld atlas blocks. Search, fitted parameters, training
        # scores and work must remain exactly unchanged; acceptance must fail.
        original=bsp.read_bytes(); changed=parts(original); atlas=bytearray(changed[14]); size=result['baked_comparison']['page_size']
        by_index={s['index']:s for s in result['samples']}; modified=set()
        for observation in fit['validation_observations']:
            if not observation['withheld']: continue
            baked=by_index[observation['sample']]['baked_lightmap']; x,y=baked['texel']
            offset=((baked['page']*size+y)*size+x)*3
            if offset in modified: continue
            modified.add(offset)
            for channel in range(3): atlas[offset+channel]=min(254,atlas[offset+channel]+20)
        changed[14]=bytes(atlas); bsp.write_bytes(pack(original,changed))
        try:
            withheld=compare(folder,game,bsp,'withheld-altered')['point_fit']
            for field in ('training','best_trial','initial_grid_alternatives','work_used'):
                assert withheld[field]==fit[field],field
            assert not withheld['accepted'] and withheld['status']=='validation_rejected'
        finally: bsp.write_bytes(original)
        solid={**payload,'fit_point_lights':{**payload['fit_point_lights'],'mins':[-140,-140,-14],'maxs':[-130,-130,-4]}}
        assert compare(folder,game,bsp,'solid-search',solid)['point_fit']['status']=='no_usable_candidates'
        for mode in ('absent','saturated'):
            changed=parts(original); changed[14]=b'' if mode=='absent' else bytes([255])*len(changed[14])
            bsp.write_bytes(pack(original,changed))
            try:
                empty=compare(folder,game,bsp,mode)['point_fit']
                assert empty['status']=='insufficient_observations' and not empty['best_trial']
                assert empty['training']['trial']['rmse_bytes'] is None
            finally: bsp.write_bytes(original)
        shader=folder/'baseq3/scripts/q3mapx_tests.shader'; script=shader.read_text()
        # Explicit light-image failure must remain a failure even if an implicit
        # fallback texture from an earlier run is available.
        shader.write_text(script.replace('{','{\n    q3map_lightimage textures/q3mapx/unavailable.tga',1))
        try:
            missing=compare(folder,game,bsp,'missing-material')['point_fit']
            assert missing['status']=='unresolved_materials' and not missing['best_trial']
            assert missing['exclusions']['missing_requested_light_image']==1
        finally: shader.write_text(script)
        # An actual implicit material needs a deliberate assumption; a default
        # image is never accepted by that override.
        (folder/'baseq3/textures/q3mapx/stone.tga').write_bytes((folder/'baseq3/textures/q3mapx/checker.tga').read_bytes())
        shader.write_text('')
        try:
            assert compare(folder,game,bsp,'implicit-unresolved')['point_fit']['status']=='unresolved_materials'
            assumed={**payload,'fit_point_lights':{**payload['fit_point_lights'],'allow_implicit_materials':True}}
            assert compare(folder,game,bsp,'implicit-assumed',assumed)['point_fit']['accepted']
        finally: shader.write_text(script)
        wrong=compare(folder,game,bsp,'wrong-gamma',settings=['-gamma',2.2])['point_fit']
        assert not wrong['accepted'] and wrong['status']=='validation_rejected'

    # Multiple stripped sources overlap, and opaque detail changes visibility.
    second={'origin':[81,-63,65],'intensity':155,'color':[1,.2,.05]}
    for mode,settings in [('multiple',[]),('entity-srgb',['-sRGBcolor']),('occlusion',[])]:
        folder=root/mode; game='quake3'; source=plain_scene(folder,game)
        text=re.sub(r'\{\s*"classname" "light"[^{}]*\}\s*','',source.read_text())
        truths=[truth,second] if mode=='multiple' else [truth]
        if mode=='occlusion':
            marker='\n}\n{\n"classname" "info_player_deathmatch"'
            assert text.count(marker)==1
            text=text.replace(marker,'\n'+box((-16,-96,0),(16,96,128))+marker)
        source.write_text(text+''.join(entity(light) for light in truths))
        base=['-game',game,'-fs_basepath',folder,'-fs_homepath',root/'home','-threads',4]
        run(exe,[*base,'-meta','-keeplights',source],folder,'bsp'); run(exe,[*base,'-vis',source],folder,'vis')
        run(exe,[*base,'-light',*common,*color_space_options(settings),source],folder,'bake')
        bsp=source.with_suffix('.bsp'); original=bsp.read_bytes(); stripped=strip_lights(original); bsp.write_bytes(stripped)
        source.write_text('original map is not available\n'); source.with_suffix('.srf').write_text('original surface extras unavailable\n')
        request={**payload,'fit_point_lights':{**payload['fit_point_lights'],'max_lights':len(truths)}}
        result=compare(folder,game,bsp,'blind',request,settings); fit=result['point_fit']
        assert fit['accepted'] and len(fit['best_trial'])==len(truths),fit
        errors=[min(math.dist(light['origin'],r['origin']) for r in fit['best_trial']) for light in truths]
        assert max(errors)<4,errors
        cases[-1]['position_errors']=errors
        rebake(folder,game,bsp,result,settings)
        if mode=='multiple':
            locked=parts(stripped); locked[0]=locked[0].rstrip(b'\0')+entity(truth).encode()+b'\0'
            bsp.write_bytes(pack(stripped,locked))
            result=compare(folder,game,bsp,'one-retained'); fit=result['point_fit']
            assert fit['accepted'] and len(fit['best_trial'])==1
            assert math.dist(fit['best_trial'][0]['origin'],second['origin'])<3
            assert len([s for s in result['sources'] if s['bsp_entity'] is not None])==1
            rebake(folder,game,bsp,result)

    for mode in ('sun','emitter','mixed-bounce'):
        folder=root/mode; source=create_lighting_fixture(folder)
        text=re.sub(r'\{\n"classname" "light"\n[^{}]*\}\n','',source.read_text())
        if mode=='sun': text=text.replace('q3mapx/emitter','q3mapx/stone')
        if mode=='emitter': text=text.replace('q3mapx/sky','q3mapx/stone')
        source.write_text(text)
        base=['-game','quake3','-fs_basepath',folder,'-fs_homepath',root/'home','-threads',4]
        run(exe,[*base,'-meta','-keeplights',source],folder,'bsp'); run(exe,[*base,'-vis',source],folder,'vis')
        run(exe,[*base,'-light',*common,*color_space_options(),*(['-bounce',1] if mode=='mixed-bounce' else []),source],folder,'bake')
        bsp=source.with_suffix('.bsp'); source.write_text('unavailable\n'); source.with_suffix('.srf').write_text('unavailable\n')
        result=compare(folder,'quake3',bsp,'no-point',payload); fit=result['point_fit']
        assert not fit['accepted']
        if mode!='mixed-bounce': assert fit['status']=='baseline_explains_observations'
        else: assert fit['status']=='validation_rejected' and fit['withheld']['trial']['rmse_bytes']>fit['max_rmse']

    (root/'results.json').write_text(json.dumps({'cases':cases,'preserved_outputs':failures},indent=2)+'\n')
    print(f'{len(cases)} point-fit reports and {len(failures)} preserved failures passed')


if __name__=='__main__': main()

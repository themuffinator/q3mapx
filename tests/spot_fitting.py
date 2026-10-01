"""Blind native spotlight fitting, target-link proposals and recovered-map bakes."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import subprocess
import time

from fixtures import create_lighting_fixture
from integration import run
from light_probes import color_space_options, plain_scene
from lighting_evidence import parts, pack
from point_fitting import entity


def marker(origin, name, classname='info_null', extra=''):
    return ('{\n"classname" "'+classname+'"\n"origin" "'+' '.join(map(str,origin))+
            '"\n"targetname" "'+name+'"\n'+extra+'}\n')


def spotlight(light, name):
    return entity(light)[:-2]+'"target" "'+name+'"\n"radius" "'+str(light['radius'])+'"\n}\n'


def direction(light):
    delta=[b-a for a,b in zip(light['origin'],light['target'])]
    length=math.sqrt(sum(v*v for v in delta))
    return [v/length for v in delta],(light['radius']+16)/length


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,required=True); p.add_argument('--work-dir',type=Path,required=True)
    p.add_argument('--case',action='append',help='Run only named fixture(s) for focused investigation')
    p.add_argument('--reference',type=Path,help='Optional previous compiler for the dangling-target regression')
    a=p.parse_args(); exe=a.compiler.resolve(); root=a.work_dir.resolve(); root.mkdir(parents=True,exist_ok=True)
    common=['-q3','-gamma',1,'-compensate',1,'-lightanglehl',0,'-nofastpoint']
    request={'schema_version':1,'baked_lightmaps':{'stride':1,'normal_offset':1},
             'fit_spot_lights':{'grid_spacing':96,'max_lights':1,'refinement_steps':8,'max_work':700000000}}
    cases=[]; failures=[]; isolation={}

    def command(folder,game,threads=4):
        return ['-game',game,'-fs_basepath',folder,'-fs_homepath',root/'home','-fs_basegame','baseq3','-threads',threads]

    def fit(folder,game,bsp,label,payload=request,options=(),threads=4):
        before=bsp.read_bytes(); source=folder/(label+'-request.json'); dest=folder/(label+'.json')
        source.write_text(json.dumps(payload)); protected={source:source.read_bytes(),bsp:before}
        for suffix in ('.map','.srf'):
            path=bsp.with_suffix(suffix)
            if path.exists(): protected[path]=path.read_bytes()
        started=time.perf_counter()
        run(exe,[*command(folder,game,threads),'-light','-probes',source,'-probe-report',dest,
                 *common,*color_space_options(options),bsp],folder,label,timeout=900)
        assert all(path.read_bytes()==contents for path,contents in protected.items())
        assert not list(folder.glob('*.q3mapx-*'))
        report=json.loads(dest.read_text()); result=report['spot_fit']
        assert report['source_sha256']==hashlib.sha256(before).hexdigest()
        assert report['settings']['lightmaps_srgb']==('-sRGBlight' in options)
        by_index={s['index']:s for s in report['samples']}; blocks={}; errors=[[],[]]; illuminated=[[],[]]
        for observation in result['validation_observations']:
            sample=by_index[observation['sample']]['baked_lightmap']
            key=(sample['page'],*(v//result['atlas_block_size'] for v in sample['texel']))
            assert blocks.setdefault(key,observation['withheld'])==observation['withheld']
            if 'trial_rgb' not in observation: continue
            residual=[a-b for a,b in zip(observation['trial_rgb'],sample['observed_rgb'])]
            errors[observation['withheld']].extend(residual)
            if observation['illuminated_support']: illuminated[observation['withheld']].extend(residual)
        for prefix,groups in (('',errors),('illuminated_',illuminated)):
            if not any(groups): continue
            for split,values in zip(('training','withheld'),groups):
                actual=result[split][prefix+'trial']; assert actual['samples']*3==len(values)
                if values:
                    assert abs(actual['rmse_bytes']-math.sqrt(sum(v*v for v in values)/len(values)))<1e-12
                    assert actual['maximum_error_bytes']==max(map(abs,values))
        cases.append({'case':folder.name+'-'+label,'status':result['status'],'accepted':result['accepted'],
                      'trial':result['best_trial'],'training':result['training'],'withheld':result['withheld'],
                      'work':result['work_used'],'seconds':time.perf_counter()-started})
        print(cases[-1]['case'],result['status'],result['work_used'],flush=True)
        return report

    def rebuild(folder,game,bsp,report,options=()):
        original=bsp.read_bytes(); output=folder/'baseq3/maps/reconstructed.map'; base=command(folder,game)
        run(exe,[*base,'-decompile','-brush-order','rebuild','-o',output,bsp],folder,'recover')
        text=output.read_text()
        for light in report['spot_fit']['best_trial']:
            link=light['target_link']; text+=spotlight(light,link['targetname'])
            if link['bsp_entity'] is None: text+=marker(link['origin'],link['targetname'],link['classname'])
            else: assert '"'+link['targetname']+'"' in text
        output.write_text(text)
        run(exe,[*base,'-meta','-keeplights',output],folder,'rebuild-bsp'); run(exe,[*base,'-vis',output],folder,'rebuild-vis')
        run(exe,[*base,'-light',*common,*color_space_options(options),output],folder,'rebuild-light',timeout=180)
        evidence=folder/'rebuilt-evidence.json'
        run(exe,[*base,'-bsp-evidence','-lighting','-lighting-stride',1,'-report',evidence,output.with_suffix('.bsp')],folder,'rebuild-evidence')
        key=lambda position,normal,style:(*(round(v,3) for v in position),*(round(v,3) for v in normal),style)
        samples={}
        for surface in json.loads(evidence.read_text())['baked_lighting']['surfaces']:
            for slot in surface['lightmap_slots']:
                for sample in slot['observations']:
                    if sample['ambiguous_mapping']: continue
                    location=key(sample['position'],sample['normal'],slot['style'])
                    assert samples.setdefault(location,sample['rgb'])==sample['rgb']
        errors=[]; transport=[]; lit=[]; by_index={s['index']:s for s in report['samples']}
        for observation in report['spot_fit']['validation_observations']:
            source=by_index[observation['sample']]; baked=source['baked_lightmap']
            actual=samples[key(source['position'],source['normal'],baked['style'])]
            residual=[a-b for a,b in zip(actual,baked['observed_rgb'])]
            errors.extend(residual); transport.extend(a-b for a,b in zip(actual,observation['trial_rgb']))
            if observation['illuminated_support']: lit.extend(residual)
        assert max(map(abs,transport))<=1
        score=lambda values:math.sqrt(sum(v*v for v in values)/len(values))
        assert score(errors)<2 and score(lit)<2
        cases[-1].update(rebuild_rmse=score(errors),illuminated_rebuild_rmse=score(lit),
                         rebuild_transport_maximum=max(map(abs,transport)))
        assert bsp.read_bytes()==original

    def fail(folder,game,bsp,label,payload,needle):
        before=bsp.read_bytes(); source=folder/(label+'-invalid.json'); source.write_text(json.dumps(payload))
        dest=folder/'preserved.json'; dest.write_text('preserved\n')
        args=[exe,*command(folder,game),'-light','-probes',source,'-probe-report',dest,*common,*color_space_options(),bsp]
        process=subprocess.run(list(map(str,args)),cwd=folder,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=900)
        log=process.stdout.decode(errors='replace'); (folder/(label+'-failure.log')).write_text(log)
        assert process.returncode==1 and needle in log,(label,process.returncode,log[-1500:])
        assert bsp.read_bytes()==before and dest.read_text()=='preserved\n' and not list(folder.glob('*.q3mapx-*'))
        failures.append(label)

    truth={'origin':[-73,21,177],'intensity':210,'color':[.35,.65,1],'target':[42,-20,0],'radius':48}
    modes=[('retained','quake3',[]),('stripped','quake3',[]),('styled','ja',[]),('srgb','qfusion',['-sRGBlight']),
           ('wide','quake3',[]),('overlap','quake3',[]),('fixed-point','quake3',[]),('duplicate-target','quake3',[]),
           ('unsafe-target','quake3',[]),('name-collision','quake3',[]),('dangling-reference','quake3',[]),
           ('ignored-target','quake3',[])]
    for mode,game,options in modes:
        if a.case and mode not in a.case: continue
        folder=root/mode; source=plain_scene(folder,game); text=source.read_text()
        if mode!='fixed-point': text=re.sub(r'\{\s*"classname" "light"[^{}]*\}\s*','',text)
        light={**truth,'style':7} if mode=='styled' else dict(truth)
        if mode=='wide': light.update(origin=[-60,22,200],target=[30,-10,0],radius=210)
        truths=[light]
        if mode=='overlap': truths.append({'origin':[50,-65,170],'target':[-50,45,0],'intensity':155,'color':[1,.2,.05],'radius':70})
        original_entities=[]; target_entities=[]
        for i,light in enumerate(truths):
            original_entities.append(spotlight(light,'original_aim_'+str(i)))
            target_entities.append(marker(light['target'],'original_aim_'+str(i),
                'info_player_deathmatch' if mode=='unsafe-target' else 'info_null'))
        source.write_text(text+''.join(original_entities+target_entities))
        base=command(folder,game)
        run(exe,[*base,'-meta','-keeplights',source],folder,'bsp'); run(exe,[*base,'-vis',source],folder,'vis')
        run(exe,[*base,'-light',*common,*color_space_options(options),source],folder,'bake',timeout=180)
        bsp=source.with_suffix('.bsp'); original=bsp.read_bytes(); lumps=parts(original)
        for light in original_entities:
            assert lumps[0].count(light.encode())==1
            lumps[0]=lumps[0].replace(light.encode(),b'')
        assert all(light.encode() not in lumps[0] for light in original_entities)
        if mode not in ('retained','duplicate-target','unsafe-target','ignored-target'):
            for target in target_entities: lumps[0]=lumps[0].replace(target.encode(),b'')
            assert b'original_aim_' not in lumps[0]
        if mode=='duplicate-target': lumps[0]=lumps[0].rstrip(b'\0')+marker([0,0,0],'original_aim_0').encode()+b'\0'
        if mode=='name-collision': lumps[0]=lumps[0].rstrip(b'\0')+marker([0,0,24],'_q3mapx_inferred_target_0','info_player_deathmatch').encode()+b'\0'
        if mode=='dangling-reference':
            lumps[0]=lumps[0].rstrip(b'\0')+b'{\n"classname" "target_relay"\n"custom_reference" "_q3mapx_inferred_target_0"\n}\n\0'
        bsp.write_bytes(pack(original,lumps)); source.write_text('hidden map unavailable\n'); source.with_suffix('.srf').write_text('hidden extras unavailable\n')
        payload={**request,'fit_spot_lights':{**request['fit_spot_lights'],'max_lights':len(truths),'style':7 if mode=='styled' else 0}}
        if mode=='overlap': payload['fit_spot_lights']['max_work']=1000000000
        if mode=='ignored-target': payload['fit_spot_lights']['use_retained_targets']=False
        report=fit(folder,game,bsp,'blind',payload,options); result=report['spot_fit']
        assert all(source['type']!='spot' for source in report['sources'])
        assert result['accepted'] and len(result['best_trial'])==len(truths),{k:result[k] for k in ('status','training','withheld','best_trial')}
        positions=[]; angles=[]; cones=[]
        for expected in truths:
            candidate=min(result['best_trial'],key=lambda light:math.dist(expected['origin'],light['origin']))
            wanted,slope=direction(expected)
            positions.append(math.dist(expected['origin'],candidate['origin']))
            angles.append(math.degrees(math.acos(max(-1,min(1,sum(a*b for a,b in zip(wanted,candidate['direction'])))))))
            cones.append(abs(math.degrees(math.atan(slope))-candidate['half_angle_degrees']))
        assert max(positions)<6 and max(angles)<2 and max(cones)<2,(mode,positions,angles,cones)
        cases[-1].update(position_errors=positions,direction_errors_degrees=angles,cone_errors_degrees=cones)
        links=[light['target_link'] for light in result['best_trial']]
        if mode=='retained': assert links[0]['bsp_entity'] is not None and links[0]['targetname']=='original_aim_0'
        else: assert all(link['bsp_entity'] is None for link in links)
        if mode in ('duplicate-target','unsafe-target'): assert not result['eligible_retained_targets']
        if mode in ('name-collision','dangling-reference'): assert links[0]['targetname']=='_q3mapx_inferred_target_1'
        if mode=='ignored-target': assert not result['eligible_retained_targets'] and result['target_exclusions']['disabled_by_request']==1
        rebuild(folder,game,bsp,report,options)
        if mode!='stripped': continue
        serial=fit(folder,game,bsp,'one-worker',payload,options,1); assert serial==report
        exact={**payload,'fit_spot_lights':{**payload['fit_spot_lights'],'max_work':result['work_used']}}
        assert fit(folder,game,bsp,'exact-work',exact)['spot_fit']['best_trial']==result['best_trial']
        fail(folder,game,bsp,'work-one-below',{**exact,'fit_spot_lights':{**exact['fit_spot_lights'],'max_work':result['work_used']-1}},'work budget exceeded')
        for key,value,needle in [('min_half_angle_degrees',0,'outside supported range'),('max_half_angle_degrees',5,'half-angle range'),
                                  ('max_spot_candidates',1,'candidate limit'),('refine_candidates',0,'outside supported range'),
                                  ('use_retained_targets',1,'must be boolean')]:
            fail(folder,game,bsp,key,{**payload,'fit_spot_lights':{**payload['fit_spot_lights'],key:value}},needle)
        fail(folder,game,bsp,'two-families',{**payload,'fit_point_lights':{}},'one fitting family')
        # Withheld values must not guide residual seeds, target selection or polishing.
        original=bsp.read_bytes(); changed=parts(original); atlas=bytearray(changed[14]); size=report['baked_comparison']['page_size']
        by_index={s['index']:s for s in report['samples']}; modified=set()
        for observation in result['validation_observations']:
            if not observation['withheld']: continue
            sample=by_index[observation['sample']]['baked_lightmap']; x,y=sample['texel']; offset=((sample['page']*size+y)*size+x)*3
            if offset in modified: continue
            modified.add(offset)
            for channel in range(3): atlas[offset+channel]=min(254,atlas[offset+channel]+20)
        changed[14]=bytes(atlas); bsp.write_bytes(pack(original,changed))
        try:
            altered=fit(folder,game,bsp,'altered-withheld',payload)['spot_fit']
            for field in ('best_trial','training','initial_grid_alternatives','work_used','residual_direction_seeds'):
                assert altered[field]==result[field],field
            assert not altered['accepted'] and altered['status']=='validation_rejected'
        finally: bsp.write_bytes(original)
        # A temporary proposed target must not turn an unrelated retained light
        # with a dangling name into a spotlight. Check the original native bug.
        isolated=folder/'target-isolation.bsp'; modified=parts(original)
        isolated_light=entity({'origin':[0,0,192],'intensity':125,'color':[1,1,1]})[:-2]
        isolated_light+='"target" "_q3mapx_probe_target_0_0"\n}\n'
        modified[0]=modified[0].rstrip(b'\0')+isolated_light.encode()+b'\0'; isolated.write_bytes(pack(original,modified))
        points=[{'surface':sample['surface'],'position':sample['position'],'normal':sample['normal']} for sample in report['samples'][:20]]
        def isolation_probe(compiler,label,proposed):
            path=folder/(label+'-request.json'); destination=folder/(label+'.json')
            payload={'schema_version':1,'samples':points}
            if proposed: payload['lights']=[{'origin':[-32,0,160],'target':[0,0,0],'radius':48,'intensity':180}]
            path.write_text(json.dumps(payload)); before=isolated.read_bytes()
            run(compiler,[*command(folder,game),'-light','-probes',path,'-probe-report',destination,
                          *common,*color_space_options(),isolated],folder,label)
            assert isolated.read_bytes()==before
            result=json.loads(destination.read_text())
            retained=[s for s in result['sources'] if s['bsp_entity'] is not None]
            assert len(retained)==1
            index=retained[0]['index']
            return retained[0]['type'],[[r['linear_rgb'] for r in sample['responses'] if r['source']==index] for sample in result['samples']]
        before=isolation_probe(exe,'isolation-baseline',False); after=isolation_probe(exe,'isolation-proposed',True)
        assert before[0]=='point' and before==after
        isolation={'current_retained_type':after[0],'unchanged_retained_responses':True,'reports':2}
        if a.reference:
            old=isolation_probe(a.reference.resolve(),'isolation-reference',True)
            assert old[0]=='spot' and old!=before
            isolation['reference_retained_type']=old[0]

    for mode in ('sun','emitter','point','undersupported','mixed-bounce'):
        if a.case and mode not in a.case: continue
        folder=root/mode
        source=plain_scene(folder,'quake3') if mode in ('point','undersupported') else create_lighting_fixture(folder)
        if mode=='undersupported':
            text=re.sub(r'\{\s*"classname" "light"[^{}]*\}\s*','',source.read_text())
            tiny={**truth,'intensity':900,'radius':1}
            source.write_text(text+spotlight(tiny,'tiny_aim')+marker(tiny['target'],'tiny_aim'))
        elif mode!='point':
            text=re.sub(r'\{\s*"classname" "light"[^{}]*\}\s*','',source.read_text())
            source.write_text(text.replace('q3mapx/emitter','q3mapx/stone') if mode=='sun' else text.replace('q3mapx/sky','q3mapx/stone') if mode=='emitter' else text)
        base=command(folder,'quake3')
        run(exe,[*base,'-meta','-keeplights',source],folder,'bsp'); run(exe,[*base,'-vis',source],folder,'vis')
        run(exe,[*base,'-light',*common,*color_space_options(),*(['-bounce',1] if mode=='mixed-bounce' else []),source],folder,'bake',timeout=180)
        bsp=source.with_suffix('.bsp')
        if mode in ('point','undersupported'):
            original=bsp.read_bytes(); lumps=parts(original); lumps[0]=re.sub(rb'\{\s*"classname" "light"[^{}]*\}\s*',b'',lumps[0]); bsp.write_bytes(pack(original,lumps))
        source.write_text('unavailable\n'); source.with_suffix('.srf').write_text('unavailable\n')
        control={**request,'baked_lightmaps':{'stride':2,'normal_offset':1}} if mode=='mixed-bounce' else request
        result=fit(folder,'quake3',bsp,'control',control)['spot_fit']
        assert not result['accepted']
        if mode in ('sun','emitter'): assert result['status']=='baseline_explains_observations'
        if mode=='undersupported': assert result['training']['illuminated_baseline']['samples']<12

    (root/'results.json').write_text(json.dumps({'cases':cases,'preserved_outputs':failures,'target_isolation':isolation},indent=2)+'\n')
    print(f'{len(cases)} spotlight reports and {len(failures)} preserved failures passed',flush=True)


if __name__=='__main__': main()

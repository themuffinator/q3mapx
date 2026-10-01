"""Apply native light fits to recovered MAPs and independently rebuild their lighting.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess

from integration import run
from light_probes import color_space_options, plain_scene
from lighting_evidence import parts, pack
from point_fitting import entity
from recovery_groups import entity_blocks, fixture as group_fixture
from spot_fitting import marker, spotlight


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--work-dir',type=Path,required=True)
    parser.add_argument('--reference',type=Path)
    parser.add_argument('--case',action='append')
    args=parser.parse_args(); exe=args.compiler.resolve(); root=args.work_dir.resolve(); root.mkdir(parents=True,exist_ok=True)
    cases=[]; failures=[]; parity=[]; fits={}
    truth={'origin':[-73,21,177],'intensity':210,'color':[.35,.65,1]}
    spot={**truth,'target':[42,-20,0],'radius':48}
    fixed_point={'origin':[-64,0,192],'intensity':200,'color':[.4,.8,1]}
    common=['-q3','-gamma',1,'-compensate',1,'-lightanglehl',0,'-nofastpoint']

    def lighting_options(settings):
        # Avoid contradictory -q3/-wolf flags: native parsing groups options.
        return [*(common[1:] if '-wolf' in settings else common),*color_space_options(settings)]

    def base(folder,game,threads=4):
        return ['-game',game,'-fs_basepath',folder,'-fs_homepath',root/'home','-fs_basegame','baseq3','-threads',threads]

    def properties(text):
        return [dict(re.findall(r'^\s*"([^"\n]+)"\s+"([^"\n]*)"',block,re.M)) for block in entity_blocks(text)]

    def export(folder,game,bsp,report_path,label,options=(),compiler=exe):
        output=folder/'baseq3/maps'/(label+'.map'); report=Path(str(output)+'.recovery.json')
        protected={p:p.read_bytes() for p in (bsp,report_path,bsp.with_suffix('.map'),bsp.with_suffix('.srf'))}
        run(compiler,[*base(folder,game),'-decompile','-brush-order','rebuild','-light-proposals',report_path,
                      '-o',output,*options,bsp],folder,label)
        assert all(p.read_bytes()==value for p,value in protected.items())
        metadata=json.loads(report.read_text()); recovery=metadata['light_recovery']; blocks=properties(output.read_text())
        assert metadata['entities']==len(blocks)
        assert recovery['proposal_sha256']==hashlib.sha256(report_path.read_bytes()).hexdigest()
        assert recovery['source_sha256']==hashlib.sha256(bsp.read_bytes()).hexdigest()
        assert recovery['stored_texels_and_scores_verified'] and not recovery['forward_transport_revalidated']
        assert not recovery['existing_entities_retargeted'] and not recovery['original_author_lights_proven']
        assert recovery['generated_entities']==len(recovery['emitted_entities'])
        for row in recovery['emitted_entities']:
            assert blocks[row['map_entity']]==row['keys']
            if row['role'] in ('inferred_target','fixed_hypothesis_target'):
                assert row['keys']['targetname'] not in ('_q3mapx_inferred_target_0','_q3mapx_fixed_target_0')
        original=properties(parts(bsp.read_bytes())[0].rstrip(b'\0').decode())
        for before,after in zip(original,blocks):
            if before['classname']=='worldspawn' or before.get('model','').startswith('*'): continue
            assert before==after,(label,before,after)
        names={}
        for block in blocks:
            if 'targetname' in block: names.setdefault(block['targetname'],[]).append(block)
        for row in recovery['emitted_entities']:
            e=row['keys']
            if e['classname']=='light' and 'target' in e:
                assert len(names[e['target']])==1
                assert names[e['target']][0]['classname'] in ('info_null','target_position')
        assert not list(folder.glob('*.q3mapx-*'))
        return output,metadata

    def rebuild(folder,game,bsp,proposal,output,label,settings):
        protected=bsp.read_bytes()
        for suffix,options in [('bsp',['-meta','-keeplights']),('vis',['-vis']),('light',['-light',*lighting_options(settings)])]:
            run(exe,[*base(folder,game),*options,output],folder,label+'-'+suffix,timeout=180)
        evidence=folder/(label+'-evidence.json')
        run(exe,[*base(folder,game),'-bsp-evidence','-lighting','-lighting-stride',1,'-report',evidence,output.with_suffix('.bsp')],folder,label+'-evidence')
        key=lambda p,n,s:(*(round(v,3) for v in p),*(round(v,3) for v in n),s)
        # Evidence preserves model-local geometry; probe fitting applies surviving
        # entity origins. Compare both in world coordinates, including inline doors.
        origins={0:[0,0,0]}
        for block in properties(parts(output.with_suffix('.bsp').read_bytes())[0].rstrip(b'\0').decode()):
            if block.get('model','').startswith('*'):
                model=int(block['model'][1:]); assert model not in origins
                origins[model]=list(map(float,block['origin'].split()))
        values={}
        for surface in json.loads(evidence.read_text())['baked_lighting']['surfaces']:
            assert surface['model_ownership']=='unique'
            origin=origins[surface['model']]
            for slot in surface['lightmap_slots']:
                for sample in slot['observations']:
                    if sample['ambiguous_mapping']: continue
                    position=[v+o for v,o in zip(sample['position'],origin)]
                    location=key(position,sample['normal'],slot['style'])
                    assert values.setdefault(location,sample['rgb'])==sample['rgb']
        fit=proposal.get('point_fit',proposal.get('spot_fit')); samples={s['index']:s for s in proposal['samples']}
        error=[]; transfer=[]
        for observation in fit['validation_observations']:
            sample=samples[observation['sample']]; baked=sample['baked_lightmap']
            actual=values[key(sample['position'],sample['normal'],baked['style'])]
            error.extend(a-b for a,b in zip(actual,baked['observed_rgb']))
            transfer.extend(a-b for a,b in zip(actual,observation['trial_rgb']))
        rmse=math.sqrt(sum(v*v for v in error)/len(error))
        assert rmse<2 and max(map(abs,transfer))<=1,(label,rmse,max(map(abs,transfer)))
        assert bsp.read_bytes()==protected
        return {'rebuild_rmse':rmse,'maximum_prediction_difference':max(map(abs,transfer)),'matched_observations':len(error)//3}

    def reject(folder,game,bsp,proposal,label,needle,options=(),raw=None):
        path=folder/(label+'-invalid.json'); path.write_text(raw if raw is not None else json.dumps(proposal))
        output=folder/'preserved.map'; recovery=folder/'preserved-recovery.json'
        output.write_bytes(b'previous map'); recovery.write_bytes(b'previous report')
        protected={p:p.read_bytes() for p in (bsp,path,output,recovery)}
        command=[exe,*base(folder,game),'-decompile','-light-proposals',path,'-o',output,'-report',recovery,*options,bsp]
        process=subprocess.run(list(map(str,command)),cwd=folder,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90)
        log=process.stdout.decode(errors='replace'); (folder/(label+'-failure.log')).write_text(log)
        assert process.returncode==1 and needle in log,(label,process.returncode,log[-2000:])
        assert all(p.read_bytes()==value for p,value in protected.items())
        assert not list(folder.glob('*.q3mapx-*'))
        failures.append(label)

    modes=[('point','quake3',[]),('spot','quake3',[]),('retained-target','ja',[]),
           ('entity-srgb','qfusion',['-sRGBlight','-sRGBcolor']),('provided-point','quake3',[]),
           ('provided-spot','quake3',[]),('retained-source','quake3',[]),
           ('wolf','wolf',['-wolf','-extradist',8]),('groups','quake3',[])]
    for mode,game,settings in modes:
        if args.case and mode not in args.case: continue
        folder=root/mode
        source=group_fixture(folder,'flat-ambiguous',patch=False)[0] if mode=='groups' else plain_scene(folder,game)
        text=re.sub(r'\{\s*"classname" "light"[^{}]*\}\s*','',source.read_text())
        is_spot=mode in ('spot','retained-target','provided-point')
        missing={**(spot if is_spot else truth),'style':7 if mode=='retained-target' else 0,'spawnflags':1 if mode=='wolf' else 0}
        fixed=[]
        if mode=='provided-point': fixed=[fixed_point,{'origin':[136,0,128],'intensity':200,'color':[1,.25,.1]}]
        if mode in ('provided-spot','retained-source'): fixed=[spot]
        source_lights=[]; targets=[]
        for i,light in enumerate([missing,*fixed]):
            name='original_aim_'+str(i)
            source_lights.append(spotlight(light,name) if 'target' in light else entity(light))
            if 'target' in light: targets.append(marker(light['target'],name))
        # Dangling custom references reserve names even when they are not native target keys.
        text+='{\n"classname" "target_relay"\n"custom_reference" "_q3mapx_inferred_target_0 _q3mapx_fixed_target_0"\n}\n'
        source.write_text(text+''.join(source_lights+targets))
        for suffix,options in [('bsp',['-meta','-keeplights']),('vis',['-vis']),('bake',['-light',*lighting_options(settings)])]:
            run(exe,[*base(folder,game),*options,source],folder,suffix,timeout=180)
        bsp=source.with_suffix('.bsp'); original=bsp.read_bytes(); lumps=parts(original)
        for light in source_lights[:1] if mode=='retained-source' else source_lights:
            assert lumps[0].count(light.encode())==1,(mode,light,lumps[0])
            lumps[0]=lumps[0].replace(light.encode(),b'')
        if mode not in ('retained-target','retained-source'):
            for target in targets: lumps[0]=lumps[0].replace(target.encode(),b'')
        bsp.write_bytes(pack(original,lumps)); source.write_text('original MAP unavailable\n'); source.with_suffix('.srf').write_text('original SRF unavailable\n')
        fit_key='fit_spot_lights' if is_spot else 'fit_point_lights'
        request={'schema_version':1,'baked_lightmaps':{'stride':4 if mode=='groups' else 1 if is_spot else 2,'normal_offset':1},
                 fit_key:{'grid_spacing':96,'max_lights':1,'refinement_steps':8,'max_work':700000000,'style':missing['style']}}
        if fixed and mode!='retained-source': request['lights']=fixed
        request_path=folder/'fit-request.json'; request_path.write_text(json.dumps(request)); path=folder/'fit.json'
        run(exe,[*base(folder,game),'-light','-probes',request_path,'-probe-report',path,*lighting_options(settings),bsp],folder,'fit',timeout=900)
        proposal=json.loads(path.read_text()); fitted=proposal['spot_fit' if is_spot else 'point_fit']
        assert proposal['settings']['wolf']==(mode=='wolf')
        assert proposal['settings']['entity_colors_srgb']==('-sRGBcolor' in settings)
        assert proposal['settings']['lightmaps_srgb']==('-sRGBlight' in settings)
        assert fitted['accepted'],(mode,fitted['status'],fitted['training'],fitted['withheld'])
        assert proposal['fixed_proposals']==request.get('lights',[])
        if mode=='provided-point': assert len([s for s in proposal['sources'] if s['proposed_light'] is not None])<len(fixed)
        fits[mode]=(folder,game,bsp,path,proposal)
        variants=[(f,fast) for f in ('map','map_bp','map_220') for fast in (False,True)] if mode in ('point','spot') else [('map_220',False)]
        for format,fast in variants:
            label='recovered-'+format+('-fast' if fast else '')
            options=['-format',format,*(['-fast'] if fast else [])]
            if mode=='groups': options+=['-group-policy','surfaces','-detail-policy','cells']
            output,metadata=export(folder,game,bsp,path,label,options)
            recovery=metadata['light_recovery']; expected_fixed=len(fixed) if mode!='retained-source' else 0
            assert recovery['fitted_lights']==1 and recovery['fixed_hypothesis_lights']==expected_fixed
            assert recovery['retained_target_links']==int(mode=='retained-target')
            if mode=='groups': assert metadata['group_inference']['exported_groups']>0
            result={'case':mode+'-'+label,'fitted_lights':1,'fixed_lights':expected_fixed,
                    'retained_target_links':recovery['retained_target_links'],'generated_entities':recovery['generated_entities'],
                    **rebuild(folder,game,bsp,proposal,output,label,settings)}
            cases.append(result); print(result['case'],result['rebuild_rmse'],flush=True)
        # The previous report schema remains usable when it has no fixed dependencies.
        if not request.get('lights'):
            old=copy.deepcopy(proposal); old.pop('fixed_proposals'); old_path=folder/'older-report.json'; old_path.write_text(json.dumps(old))
            export(folder,game,bsp,old_path,'older-schema')
        if mode=='point':
            output,_=export(folder,game,bsp,path,'serial',['-threads',1])
            parallel,_=export(folder,game,bsp,path,'parallel')
            assert output.read_bytes()==parallel.read_bytes()
            for compiler,label in [(exe,'default-current'),*([(args.reference.resolve(),'default-reference')] if args.reference else [])]:
                output=folder/'baseq3/maps'/(label+'.map')
                run(compiler,[*base(folder,game),'-decompile','-o',output,bsp],folder,label)
            if args.reference:
                assert (folder/'baseq3/maps/default-current.map').read_bytes()==(folder/'baseq3/maps/default-reference.map').read_bytes()
                parity.append('default-map-bytes')
            # Legacy convert also requires and emits the companion provenance report.
            legacy=folder/'baseq3/maps/legacy.map'
            run(exe,[*base(folder,game),'-convert','-format','map_220','-light-proposals',path,'-o',legacy,bsp],folder,'legacy')
            assert json.loads(Path(str(legacy)+'.recovery.json').read_text())['light_recovery']['fitted_lights']==1

    if 'point' in fits:
        folder,game,bsp,path,proposal=fits['point']
        def change(label,needle,mutate,options=()):
            data=copy.deepcopy(proposal); mutate(data); reject(folder,game,bsp,data,label,needle,options)
        change('wrong-source','source BSP SHA-256',lambda d:d.update(source_sha256='0'*64))
        change('wrong-game','game profile',lambda d:d.update(game='ja'))
        change('unqualified','Unqualified',lambda d:d['point_fit'].update(accepted=False))
        change('wrong-status','status/family',lambda d:d['point_fit'].update(status='validation_rejected'))
        change('two-families','single qualified',lambda d:d.update(spot_fit=d['point_fit']))
        change('empty-trial','array',lambda d:d['point_fit'].update(best_trial=[]))
        change('too-many-lights','array',lambda d:d['point_fit'].update(best_trial=d['point_fit']['best_trial']*17))
        change('missing-origin','Missing',lambda d:d['point_fit']['best_trial'][0].pop('origin'))
        change('huge-origin','range',lambda d:d['point_fit']['best_trial'][0].update(origin=[1e9,0,0]))
        change('wrong-energy','intensity',lambda d:d['point_fit']['best_trial'][0].update(intensity=1))
        change('wrong-flags','style/flags',lambda d:d['point_fit']['best_trial'][0].update(spawnflags=1))
        change('wrong-score','score is inconsistent',lambda d:d['point_fit']['training']['trial'].update(rmse_bytes=123))
        change('wrong-count','sample count',lambda d:d['point_fit']['withheld']['trial'].update(samples=1))
        change('wrong-texel','source BSP texels',lambda d:d['samples'][d['point_fit']['validation_observations'][0]['sample']]['baked_lightmap']['observed_rgb'].__setitem__(0,254))
        change('duplicate-sample','Duplicate light recovery validation',lambda d:d['point_fit']['validation_observations'].append(d['point_fit']['validation_observations'][0]))
        change('wrong-split','split is inconsistent',lambda d:d['point_fit']['validation_observations'][0].update(withheld=not d['point_fit']['validation_observations'][0]['withheld']))
        change('score-gate','qualification gates',lambda d:d['point_fit'].update(max_rmse=.01))
        reject(folder,game,bsp,proposal,'duplicate-property','Duplicate or invalid',raw=json.dumps(proposal).replace('"accepted": true','"accepted": true, "accepted": false',1))
        reject(folder,game,bsp,proposal,'deep-json','structure limit',raw='['*70+'0'+']'*70)
        for label,options,needle in [('read-map',['-readmap'],'compiled BSP'),('wtf',['-wtf'],'material replacement'),
            ('mesh',['-format','obj'],'MAP export'),('repeat-report',['-light-proposals',path],'Only one')]:
            reject(folder,game,bsp,proposal,label,needle,options)
        # Both direct and hard-link aliases must preserve the fitting report.
        for hard in (False,True):
            alias=folder/'report-alias.json' if hard else path
            if hard:
                if alias.exists(): alias.unlink()
                os.link(path,alias)
            before=path.read_bytes(); output=folder/'preserved.map'; output.write_bytes(b'old map')
            process=subprocess.run(list(map(str,[exe,*base(folder,game),'-decompile','-light-proposals',path,'-report',alias,'-o',output,bsp])),cwd=folder,capture_output=True,timeout=90)
            assert process.returncode==1 and b'aliases a source' in process.stdout
            assert path.read_bytes()==before and output.read_bytes()==b'old map'
            failures.append('report-alias-'+str(hard))
        huge=folder/'oversized.json'
        with huge.open('wb') as stream: stream.truncate(64*1024*1024+1)
        output=folder/'preserved.map'; output.write_bytes(b'old map')
        process=subprocess.run(list(map(str,[exe,*base(folder,game),'-decompile','-light-proposals',huge,'-o',output,bsp])),cwd=folder,capture_output=True,timeout=90)
        assert process.returncode==1 and b'byte limit' in process.stdout and output.read_bytes()==b'old map'
        failures.append('report-byte-limit'); huge.unlink()
    if 'spot' in fits:
        folder,game,bsp,path,proposal=fits['spot']
        for label,needle,mutate in [
            ('target-name-collision','collides',lambda t:t['target_link'].update(targetname='_q3mapx_inferred_target_0')),
            ('target-class','status/family',lambda t:t['target_link'].update(classname='trigger_once')),
            ('target-injection','MAP syntax',lambda t:t['target_link'].update(targetname='bad"\n}')),
            ('target-position','target metadata',lambda t:t['target_link'].update(origin=[0,0,0])),
            ('target-cone','target/cone',lambda t:t.update(radius=1)),
            ('target-direction','target/cone',lambda t:t.update(direction=[0,0,0])),
            ('retarget-existing','target metadata',lambda t:t['target_link'].update(input_entity_modified=True))]:
            data=copy.deepcopy(proposal); mutate(data['spot_fit']['best_trial'][0]); reject(folder,game,bsp,data,label,needle)
        data=copy.deepcopy(proposal); data['spot_fit']['validation_observations'][0]['illuminated_support']=not data['spot_fit']['validation_observations'][0]['illuminated_support']
        reject(folder,game,bsp,data,'false-support','support is inconsistent')
    if 'provided-point' in fits:
        folder,game,bsp,path,proposal=fits['provided-point']
        for label,needle,mutate in [
            ('missing-dependency','omits fixed proposal',lambda d:d.pop('fixed_proposals')),
            ('dependency-count','array',lambda d:d.update(fixed_proposals=[])),
            ('dependency-field','Unknown light proposal',lambda d:d['fixed_proposals'][0].update(model='unexpected')),
            ('dependency-underflow','native float',lambda d:d['fixed_proposals'][0].update(intensity=1e-100)),
            ('dependency-index','integer',lambda d:d['proposed_entity_indices'].__setitem__(0,0))]:
            data=copy.deepcopy(proposal); mutate(data); reject(folder,game,bsp,data,label,needle)
    (root/'results.json').write_text(json.dumps({'cases':cases,'preserved_failures':failures,'reference_parity':parity},indent=2)+'\n')
    print(f'{len(cases)} full light-recovery rebuilds and {len(failures)} preserved failures passed',flush=True)


if __name__=='__main__': main()

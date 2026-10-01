"""Real offscreen workbench light fitting, reviewed export and independent rebakes.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re

from integration import run
from light_probes import plain_scene, color_space_options
from lighting_evidence import parts, pack
from point_fitting import entity
from spot_fitting import spotlight, marker


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test',type=Path,required=True)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--work-dir',type=Path,required=True)
    args=parser.parse_args(); exe=args.compiler.resolve(); test=args.test.resolve(); root=args.work_dir.resolve()
    root.mkdir(parents=True,exist_ok=True); os.environ['QT_QPA_PLATFORM']='offscreen'
    results=[]
    truth={'origin':[-73,21,177],'intensity':210,'color':[.35,.65,1]}
    for mode,game,srgb in [('point','quake3',False),('spot','quake3',False),('fixed-srgb','qfusion',True)]:
        folder=root/mode; game_directory='base' if game=='qfusion' else 'baseq3'
        source=plain_scene(folder,game,game_directory=game_directory)
        missing={**truth,**({'target':[42,-20,0],'radius':48} if mode=='spot' else {})}
        lights=spotlight(missing,'removed_aim')+marker(missing['target'],'removed_aim') if mode=='spot' else entity(missing)
        fixed=[{'origin':[48,48,192],'intensity':120,'color':[.8,.2,.4]}] if mode=='fixed-srgb' else []
        lights+=''.join(entity(light) for light in fixed)
        text=re.sub(r'\{\s*"classname" "light"[^{}]*\}\s*','',source.read_text())
        source.write_text(text+lights)
        base=['-game',game,'-fs_basepath',folder,'-fs_homepath',folder/'home','-fs_basegame',game_directory,'-threads',4]
        lighting=['-q3','-gamma',1,'-compensate',1,'-lightanglehl',0,'-nofastpoint',*color_space_options(['-sRGBlight','-sRGBcolor'] if srgb else [])]
        for stage,options in [('bsp',['-meta','-keeplights']),('vis',['-vis']),('light',['-light',*lighting])]:
            run(exe,[*base,*options,source],folder,'source-'+stage,timeout=180)
        bsp=source.with_suffix('.bsp'); original=bsp.read_bytes(); lumps=parts(original)
        for block in re.findall(r'\{[^{}]*\}\n',lights):
            assert lumps[0].count(block.encode())==1
            lumps[0]=lumps[0].replace(block.encode(),b'')
        bsp.write_bytes(pack(original,lumps)); source.write_text('Original MAP unavailable\n'); source.with_suffix('.srf').write_text('Original SRF unavailable\n')
        protected={p:p.read_bytes() for p in (source,bsp,source.with_suffix('.srf'))}
        project={'schema_version':1,'name':'Recovered '+mode,'source':str(bsp),'game_root':str(folder),'output_root':str(folder/'runs'),
                 'compiler':str(exe),'game':'q3' if game=='quake3' else game,'workers':4,'brush_order':'rebuild',
                 'light_fit':{'family':'spot' if mode=='spot' else 'point','max_lights':1,'refinement_steps':8,'max_work':700000000,
                              'stride':1 if mode=='spot' else 2,'lightmaps_srgb':srgb,'entity_colors_srgb':srgb,'fixed_lights':fixed}}
        path=folder/'project.q3mapx.json'; path.write_text(json.dumps(project))
        run(test,['-platform','offscreen',path,folder/'window'],folder,'window',timeout=900)
        record=json.loads((folder/'window/result.json').read_text()); output=Path(record['recovered_map']); proposal=json.loads(Path(record['fit_report']).read_text())
        assert record['checks'] and all(record['checks'].values())
        for stage,options in [('bsp',['-meta','-keeplights']),('vis',['-vis']),('light',['-light',*lighting])]:
            run(exe,[*base,*options,output],folder,'rebuilt-'+stage,timeout=180)
        evidence=folder/'rebuilt-evidence.json'; run(exe,[*base,'-bsp-evidence','-lighting','-lighting-stride',1,'-report',evidence,output.with_suffix('.bsp')],folder,'rebuilt-evidence')
        key=lambda p,n,s:(*(round(v,3) for v in p),*(round(v,3) for v in n),s)
        values={}
        for surface in json.loads(evidence.read_text())['baked_lighting']['surfaces']:
            assert surface['model']==0
            for slot in surface['lightmap_slots']:
                for sample in slot['observations']:
                    if not sample['ambiguous_mapping']:
                        loc=key(sample['position'],sample['normal'],slot['style'])
                        assert values.setdefault(loc,sample['rgb'])==sample['rgb']
        fit=proposal.get('point_fit',proposal.get('spot_fit')); error=[]; transfer=[]
        assert fit['accepted'] and proposal['fixed_proposals']==fixed
        for observation in fit['validation_observations']:
            sample=proposal['samples'][observation['sample']]; baked=sample['baked_lightmap']
            actual=values[key(sample['position'],sample['normal'],baked['style'])]
            error.extend(a-b for a,b in zip(actual,baked['observed_rgb']))
            transfer.extend(a-b for a,b in zip(actual,observation['trial_rgb']))
        rmse=math.sqrt(sum(v*v for v in error)/len(error))
        assert rmse<2 and max(map(abs,transfer))<=1
        assert all(p.read_bytes()==v for p,v in protected.items())
        results.append({'case':mode,'checks':record['checks'],'rebuild_rmse':rmse,'maximum_prediction_difference':max(map(abs,transfer)),
                        'observations':len(error)//3,'source_sha256':hashlib.sha256(bsp.read_bytes()).hexdigest()})
        print(mode,'workbench fit/export/rebuild passed; RMSE',rmse,flush=True)
    (root/'results.json').write_text(json.dumps({'cases':results},indent=2)+'\n')


if __name__=='__main__': main()

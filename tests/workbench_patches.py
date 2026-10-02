"""Native workbench patch recovery and independent compiler rebuild comparisons.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from fixtures import create_fixture
from integration import run
from patch_paint import painted
from patch_source import archive, FOOTER
from patch_reconstruction import triangles


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('test','compiler','work-dir'): p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args(); root=a.work_dir.resolve(); root.mkdir(parents=True,exist_ok=True)
    exe=a.compiler.resolve(); test=a.test.resolve(); results=[]
    for game in ('quake3','ja'):
        for mode in ('source','fit','native','auto-source','auto-fit'):
            folder=root/(game+'-'+mode); source=create_fixture(folder,patch=False); plain=source.read_text(encoding='utf-8')
            scripts=source.parent.parent/'scripts/q3mapx_tests.shader'
            material=scripts.read_text(encoding='utf-8')+'\ntextures/q3mapx/paint\n{\n'+('' if mode=='native' else 'surfaceparm nonsolid\n')+'{ map $whiteimage rgbGen vertex alphaGen vertex }\n}\n'
            scripts.write_text(material,encoding='utf-8')
            raven=source.parent.parent/'shaders'; raven.mkdir(exist_ok=True); (raven/'shaderlist.txt').write_text('q3mapx_tests\n',encoding='utf-8'); (raven/'q3mapx_tests.shader').write_text(material,encoding='utf-8')
            world=painted(subdivisions=8,size=7,curved=True)
            door=painted(subdivisions=8,size=13,curved=True,origin=(96,-48,144),extent=64)
            source.write_text(plain.replace('"message" "q3mapx regression"\n','"message" "q3mapx regression"\n'+world)
                .replace('"targetname" "test_door"\n','"targetname" "test_door"\n'+door),encoding='utf-8')
            base=['-game',game,'-fs_basegame','baseq3','-fs_basepath',folder,'-fs_homepath',folder/'home','-threads',1]
            run(exe,[*base,source],folder,'bsp'); run(exe,[*base,'-light','-fast',source],folder,'light')
            bsp=source.with_suffix('.bsp'); data=bsp.read_bytes()
            if mode not in ('source','auto-source'):
                data=data[:-FOOTER-len(archive(data))]; bsp.write_bytes(data)
            source.write_text('poisoned source',encoding='utf-8'); source.with_suffix('.srf').write_text('poisoned source extras',encoding='utf-8')
            ui=folder/'ui'; ui.mkdir(exist_ok=True)
            policy='none' if mode=='native' else 'auto' if mode.startswith('auto-') else mode
            colors='alpha' if mode=='native' else 'rgba'
            project=folder/'project.q3mapx.json'
            project.write_text(json.dumps(dict(schema_version=1,name='Patch recovery '+mode,source=str(bsp),game_root=str(folder),
                output_root=str(folder/'outputs'),compiler=str(exe),game=game,mod='baseq3',workers=1,
                patch_recovery=policy,patch_colors=colors,patch_color_subdivisions=16,patch_fit_work_limit=50_000_000)),encoding='utf-8')
            env=dict(os.environ,QT_QPA_PLATFORM='offscreen')
            result=subprocess.run([str(test),str(project),str(ui)],cwd=folder,env=env,capture_output=True,timeout=240)
            (ui/'test.log').write_bytes(result.stdout+result.stderr)
            assert result.returncode==0,(game,mode,result.returncode,(result.stdout+result.stderr)[-5000:])
            evidence=json.loads((ui/'validation.json').read_text(encoding='utf-8')); recovered=Path(evidence['recovered_map'])
            run(exe,[*base,recovered],folder,'rebuild')
            rebuilt=recovered.with_suffix('.bsp').read_bytes()
            assert triangles(data,colors)==triangles(rebuilt,colors),(game,mode,'Rebuilt oriented triangles differ')
            run(exe,[*base,'-light','-fast',recovered],folder,'relight')
            assert triangles(data,colors)==triangles(recovered.with_suffix('.bsp').read_bytes(),colors)
            assert bsp.read_bytes()==data
            results.append(dict(game=game,mode=mode,checks=evidence['checks'],triangles=len(triangles(data,colors)),
                rebuild_and_relight_equal=True,ui_evidence_sha256=hashlib.sha256((ui/'validation.json').read_bytes()).hexdigest()))
    (root/'validation.json').write_text(json.dumps(dict(cases=results,compiler_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        ui_test_sha256=hashlib.sha256(test.read_bytes()).hexdigest()),indent=2)+'\n',encoding='utf-8')
    print(f'{len(results)} native workbench patch recovery, review and rebuild cases passed')


if __name__=='__main__': main()

"""Real OpenCL execution, image parity and missing-runtime behavior. GPL-3.0-or-later."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from fixtures import create_fixture
from integration import run


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--work-dir',type=Path,required=True)
    args=parser.parse_args()
    exe,root=args.compiler.resolve(),args.work_dir.resolve()
    source=create_fixture(root,dense=True,grid=5)
    base=['-game','quake3','-fs_basepath',root,'-threads',4]
    run(exe,[*base,'-meta',source],root,'bsp')
    environment=dict(os.environ,Q3MAPX_DISABLE_GPU='1')
    devices=json.loads(subprocess.check_output([str(exe),'-devices'],cwd=root,env=environment))
    assert devices['devices']==[] and devices['reason']
    result=subprocess.run([str(exe),*map(str,base),'-minimap','-backend','gpu','-size','32',str(source)],
                          cwd=root,env=environment,capture_output=True,timeout=30)
    assert result.returncode!=0 and b'GPU minimap failed' in result.stdout
    fallback=root/'fallback.json'
    result=subprocess.run([str(exe),*map(str,base),'-minimap','-backend','auto','-random','256','-size','1024',
                           '-compute-report',str(fallback),'-o',str(root/'fallback.tga'),str(source)],
                          cwd=root,env=environment,capture_output=True,timeout=90)
    (root/'fallback.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode==0, result.stdout[-4000:]
    report=json.loads(fallback.read_text())
    assert report['backend']=='cpu' and ('disabled' in report['reason'].lower())
    devices=json.loads(subprocess.check_output([str(exe),'-devices'],cwd=root))['devices']
    if not devices:
        print('No OpenCL GPU: missing-runtime and CPU fallback checks passed; hardware parity skipped')
        return
    # Exercise native vendor drivers, avoiding duplicate Microsoft translation layers.
    devices=[d for d in devices if d['vendor']!='Microsoft'] or devices[:1]
    evidence=[]
    for number,options in enumerate(([],['-samples',4],['-random',8,'-seed',19],
                                    ['-samples',4,'-border',0.1,'-sharpen',0.5,'-boost',1.25])):
        cpu=root/'cpu.tga'
        run(exe,[*base,'-minimap','-backend','cpu','-size',127,'-o',cpu,*options,source],root,f'cpu-{number}')
        expected=cpu.read_bytes()
        for device in devices:
            gpu=root/'gpu.tga'
            reportpath=root/f"gpu-{device['index']}-{number}.json"
            run(exe,[*base,'-minimap','-backend','gpu','-gpu-device',device['index'],'-size',127,'-o',gpu,
                     '-compute-report',reportpath,*options,source],root,f"gpu-{device['index']}-{number}")
            actual=gpu.read_bytes()
            assert actual[:18]==expected[:18] and len(actual)==len(expected)
            errors=[abs(a-b) for a,b in zip(actual[18:],expected[18:])]
            assert max(errors)<=1, (device['name'],options,max(errors),sum(e>1 for e in errors))
            report=json.loads(reportpath.read_text())
            assert report['backend']=='gpu' and report['device']==device['name'] and report['kernel_seconds']>0
            evidence.append(dict(device=device,options=options,max_pixel_error=max(errors),report=report))
    (root/'hardware-parity.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print('Real GPU image parity on',', '.join(d['name'] for d in devices),'and missing-driver fallback passed')


if __name__=='__main__': main()

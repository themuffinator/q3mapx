"""Alternating whole-process minimap benchmark with pixel parity. GPL-3.0-or-later."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tests'))
from fixtures import create_fixture
from integration import run


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--work-dir',type=Path,required=True)
    parser.add_argument('--size',type=int,default=2048)
    parser.add_argument('--samples',type=int,default=4)
    parser.add_argument('--grid',type=int,default=31)
    parser.add_argument('--threads',type=int,default=20)
    parser.add_argument('--repeat',type=int,default=5)
    parser.add_argument('--gpu',action='store_true')
    parser.add_argument('--no-legacy',action='store_true',help='Compare modern CPU/GPU without the much slower original sampler')
    args=parser.parse_args()
    if args.repeat < 2: parser.error('At least two measured runs required')
    root=args.work_dir.resolve()
    source=create_fixture(root,dense=True,grid=args.grid)
    baseline,compiler=args.baseline.resolve(),args.compiler.resolve()
    base=['-game','quake3','-fs_basepath',root,'-threads',args.threads]
    run(baseline,[*base,'-meta',source],root,'bsp',timeout=300)
    methods=[('nrc',baseline,[]),('cpu',compiler,['-backend','cpu'])]
    if args.gpu: methods.append(('gpu',compiler,['-backend','gpu']))
    if args.no_legacy: methods=[method for method in methods if method[0]!='nrc']
    records={name:[] for name,_,_ in methods}
    expected=None
    errors={}
    for iteration in range(args.repeat+1):
        order=methods[iteration%len(methods):]+methods[:iteration%len(methods)]
        for name,exe,options in order:
            output=root/f'{name}.tga'
            report_args=[] if name=='nrc' else ['-compute-report',root/f'{name}-{iteration}.json']
            result=run(exe,[*base,'-minimap',*options,*report_args,'-size',args.size,'-samples',args.samples,'-o',output,source],
                       root,f'{name}-{iteration}',timeout=300)
            pixels=output.read_bytes()
            if expected is None: expected=pixels
            assert len(pixels)==len(expected)
            error=max(abs(a-b) for a,b in zip(pixels,expected))
            assert error <= (1 if name=='gpu' else 0), f'{name}: pixel difference {error}'
            errors[name]=max(errors.get(name,0),error)
            if iteration: records[name].append(result['seconds'])
            print(name,iteration,f"{result['seconds']:.4f}s",flush=True)
    report={'schema_version':1,'kind':'whole_process_minimap','timestamp_utc':datetime.now(timezone.utc).isoformat(),
            'platform':platform.platform(),'processor':platform.processor(),'logical_cpus':os.cpu_count(),
            'size':args.size,'samples':args.samples,'fixture_grid':args.grid,'threads':args.threads,
            'warmup_runs':1,'measured_runs':args.repeat,'reference_image_sha256':hashlib.sha256(expected).hexdigest(),
            'records':[{'backend':name,'compiler':str(exe),'compiler_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
                        'seconds':records[name],'median_seconds':statistics.median(records[name]),
                        'max_pixel_error':errors[name]} for name,exe,_ in methods]}
    (root/'benchmark.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({name:statistics.median(values) for name,values in records.items()}))


if __name__=='__main__': main()

"""Alternating whole-process VIS timings and visibility comparisons. GPL-3.0-or-later."""
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
from integration import Bsp, run


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=Path,required=True)
    parser.add_argument('--previous',type=Path)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--work-dir',type=Path,required=True)
    parser.add_argument('--grid',type=int,default=9)
    parser.add_argument('--threads',type=int,nargs='+',default=[1,20])
    parser.add_argument('--repeat',type=int,default=3)
    parser.add_argument('--characterize-thread-differences',action='store_true',help='Record inherited multiworker output differences instead of accepting them as parity')
    args=parser.parse_args()
    if args.repeat<2: parser.error('At least two measured runs required')
    root=args.work_dir.resolve()
    source=create_fixture(root,dense=True,grid=args.grid)
    base=['-game','quake3','-fs_basepath',root]
    methods=[('nrc',args.baseline.resolve())]
    if args.previous: methods.append(('previous',args.previous.resolve()))
    methods.append(('q3mapx',args.compiler.resolve()))
    hashes={name:hashlib.sha256(exe.read_bytes()).hexdigest() for name,exe in methods}
    run(methods[0][1],[*base,'-threads',1,'-meta',source],root,'bsp',timeout=300)
    original=source.with_suffix('.bsp').read_bytes()
    records=[]
    expected=None
    for threads in args.threads:
        times={name:[] for name,_ in methods}
        outputs={name:[] for name,_ in methods}
        for iteration in range(args.repeat+1):
            order=methods[iteration%len(methods):]+methods[:iteration%len(methods)]
            for name,exe in order:
                source.with_suffix('.bsp').write_bytes(original)
                profile=[] if name=='nrc' else ['-profile',root/f'{name}-{threads}-{iteration}.json']
                result=run(exe,[*base,'-threads',threads,*profile,'-vis','-saveprt',source],root,f'{name}-{threads}-{iteration}',timeout=600)
                visibility=Bsp(source.with_suffix('.bsp')).lump(16)
                if expected is None: expected=visibility
                assert len(visibility)==len(expected),f'{name}/{threads}: visibility size differs'
                differences=sum((a^b).bit_count() for a,b in zip(visibility,expected))
                if threads==1 or not args.characterize_thread_differences:
                    assert not differences,f'{name}/{threads}: visibility differs by {differences} bits'
                outputs[name].append({'sha256':hashlib.sha256(visibility).hexdigest(),'different_bits':differences})
                if iteration: times[name].append(result['seconds'])
                print(name,threads,iteration,f"{result['seconds']:.4f}s",f'different_bits={differences}',flush=True)
        records.extend({'implementation':name,'threads':threads,'compiler':str(exe),'compiler_sha256':hashes[name],
                        'seconds':times[name],'median_seconds':statistics.median(times[name]),'outputs_including_warmup':outputs[name]} for name,exe in methods)
    for name,exe in methods:
        assert hashlib.sha256(exe.read_bytes()).hexdigest()==hashes[name],f'{exe} changed during benchmark'
    report={'schema_version':1,'kind':'whole_process_visibility','timestamp_utc':datetime.now(timezone.utc).isoformat(),
            'platform':platform.platform(),'processor':platform.processor(),'logical_cpus':os.cpu_count(),
            'fixture_grid':args.grid,'warmup_runs':1,'measured_runs':args.repeat,
            'visibility_sha256':hashlib.sha256(expected).hexdigest(),'records':records}
    (root/'benchmark.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(records,indent=2))


if __name__=='__main__': main()

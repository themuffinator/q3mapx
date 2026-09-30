"""Matched whole-command passage measurements. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tests'))
from integration import Bsp, run
from vis_fixtures import create_vis_fixture


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def observed(log):
    memory=re.search(r'(\d+) bytes required passage memory \((\d+) passages\)',log)
    assert memory, log[-3000:]
    values={'retained_requested_bytes':int(memory[1]),'passages':int(memory[2])}
    packed=re.search(r'Passage storage: (\d+) retained / (\d+) dense bytes; (\d+) empty masks; (\d+) blocks',log)
    if packed:
        values.update(zip(('retained_requested_bytes','dense_equivalent_bytes','empty_masks','retained_blocks'),map(int,packed.groups())))
        candidates=re.search(r'Passage candidate tests: (\d+) / (\d+) dense portal visits',log)
        assert candidates
        values.update(zip(('candidate_visits','dense_visits'),map(int,candidates.groups())))
    return values


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--reference',type=Path,required=True)
    parser.add_argument('--reference-storage',choices=('dense','packed'),default='dense',
                        help='Storage layout of the reference revision (default: original dense implementation)')
    parser.add_argument('--work-dir',type=Path,required=True)
    parser.add_argument('--grid',type=int,default=9)
    parser.add_argument('--workers',type=int,nargs='+',default=[1,4])
    parser.add_argument('--repeat',type=int,default=5)
    args=parser.parse_args()
    if args.repeat<2: parser.error('At least two measured runs are required')
    root=args.work_dir.resolve(); root.mkdir(parents=True,exist_ok=True)
    reference_name='dense' if args.reference_storage=='dense' else 'packed-reference'
    methods=[(reference_name,args.reference.resolve()),('packed',args.compiler.resolve())]
    binaries={name:{'path':str(exe),'sha256':sha(exe)} for name,exe in methods}
    records,fixtures=[],[]
    for detail in (False,True):
        directory=root/('detail' if detail else 'structural')
        source=create_vis_fixture(directory,grid=args.grid,detail=detail)
        target=source.with_suffix('.bsp')
        base=['-game','quake3','-fs_basepath',directory,'-fs_homepath',directory/'home']
        run(args.compiler.resolve(),[*base,'-threads',1,'-meta',source],directory,'bsp')
        original=Bsp(target)
        fixtures.append({'detail':detail,'map_sha256':sha(source),'bsp_sha256':sha(target),
                         'prt_sha256':sha(source.with_suffix('.prt')),'counts':original.summary()})
        expected={}
        for merge in (False,True):
            for passage_only in (False,True):
                options=(['-merge'] if merge else [])+(['-passageOnly'] if passage_only else [])
                for workers in args.workers:
                    entry={name:{'seconds':[],'profiles':[]} for name,_ in methods}
                    for iteration in range(args.repeat+1):
                        order=methods if iteration%2==0 else list(reversed(methods))
                        for name,exe in order:
                            target.write_bytes(original.data)
                            label=f'{merge}-{passage_only}-{workers}-{iteration}-{name}'
                            profile=directory/(label+'.json')
                            result=run(exe,[*base,'-threads',workers,'-profile',profile,'-vis','-reproducible',
                                            '-saveprt',*options,source],directory,label,timeout=300)
                            output=Bsp(target)
                            for lump in range(1,16): assert output.lump(lump)==original.lump(lump),(label,lump)
                            key=(merge,passage_only)
                            if key not in expected: expected[key]=output.lump(16)
                            assert output.lump(16)==expected[key],(label,'reference/worker VIS differs')
                            metrics=observed((directory/(label+'.log')).read_text())
                            if 'storage' in entry[name]: assert metrics==entry[name]['storage'],(label,'unstable storage')
                            entry[name]['storage']=metrics
                            if iteration:
                                entry[name]['seconds'].append(result['seconds'])
                                measured=json.loads(profile.read_text()); passes={}
                                for item in measured['passes']:
                                    count=passes.setdefault(item['name'],{'seconds':0,'seconds_with_setup':0})
                                    for field in count: count[field]+=item[field]
                                entry[name]['profiles'].append({'path':str(profile.relative_to(root)),
                                    'sha256':sha(profile),'total_seconds':measured['total_seconds'],'passes':passes})
                    if args.reference_storage=='dense':
                        assert entry['packed']['storage']['dense_equivalent_bytes']==entry[reference_name]['storage']['retained_requested_bytes']
                    else:
                        assert entry['packed']['storage']==entry[reference_name]['storage']
                    for name in entry:
                        entry[name]['median_seconds']=statistics.median(entry[name]['seconds'])
                    record={'detail':detail,'options':options,'workers':workers,
                            'visibility_sha256':hashlib.sha256(expected[key]).hexdigest(),
                            'reference_and_worker_byte_parity':True,'implementations':entry}
                    records.append(record)
                    print(f'{directory.name} {options} workers={workers}: '+
                          ', '.join(f'{name} {entry[name]["median_seconds"]:.4f}s' for name,_ in methods),flush=True)
    for name,exe in methods: assert sha(exe)==binaries[name]['sha256'],(name,'binary changed during benchmark')
    report={'schema_version':1,'kind':'whole_process_vis_passage_storage','recorded_utc':datetime.now(timezone.utc).isoformat(),
            'platform':platform.platform(),'processor':platform.processor(),'logical_cpus':os.cpu_count(),
            'grid':args.grid,'warmup_runs':1,'measured_runs':args.repeat,'reference_storage':args.reference_storage,
            'compilers':binaries,'fixtures':fixtures,'records':records,
            'notes':['Alternating preceding/current binaries, startup/input/merge/flow/output included; no LIGHT or renderer timing.',
                     'Storage counts retained requested bytes, not allocator overhead, temporary construction allocations or peak process RSS.',
                     'The packed implementation initially reserves a dense upper bound per concurrently building portal, then shrinks it.',
                     'Both revisions use the same input and solver options. Large-winding correctness changes need separate analytic tests.',
                     'Existing merge-mode differences from default are outside this parity contract.']}
    (root/'benchmark.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__': main()

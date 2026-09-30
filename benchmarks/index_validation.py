"""Whole-process shared-index validation comparison. GPL-3.0-or-later."""
import argparse
from datetime import datetime,timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys
import time

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tests"))
from index_validation import indexed_bsp


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline",type=Path,required=True)
    parser.add_argument("--compiler",type=Path,required=True)
    parser.add_argument("--work-dir",type=Path,required=True)
    parser.add_argument("--surfaces",type=int,default=20_000)
    parser.add_argument("--indices",type=int,default=120_000)
    parser.add_argument("--repeat",type=int,default=5)
    parser.add_argument("--map",type=Path,help="Optional private native input; copied into the work directory")
    parser.add_argument("--game",default="quake3")
    args=parser.parse_args()
    if not 1<=args.surfaces<=100_000 or not 3<=args.indices<=3_000_000 or args.indices%3 or not 3<=args.repeat<=20:
        parser.error("Use 1..100000 surfaces, 3..3000000 indices divisible by three, and 3..20 repeats")
    root=args.work_dir.resolve()
    root.mkdir(parents=True,exist_ok=True)
    methods=[("baseline",args.baseline.resolve(strict=True)),("q3mapx",args.compiler.resolve(strict=True))]
    digest=lambda data:hashlib.sha256(data).hexdigest()
    hashes={name:digest(exe.read_bytes()) for name,exe in methods}
    if args.map:
        original=args.map.resolve(strict=True)
        data=original.read_bytes()
        counts=None
    else:
        if args.game!="quake3": parser.error("Generated fixture uses quake3")
        original=None
        data=indexed_bsp([0,1,2]*(args.indices//3),[(0,args.indices,3)]*args.surfaces)
        counts={"surfaces":args.surfaces,"stored_indices":args.indices,"expanded_references":args.surfaces*args.indices}
    source=root/"input.bsp"
    if original==source: parser.error("Select an output directory separate from the source")
    source.write_bytes(data)
    samples=[]
    output_contract=None
    arguments=["-game",args.game,"-fs_basepath",str(root),"-fs_homepath",str(root/"home"),"-threads","1","-info",str(source)]
    for iteration in range(args.repeat+1):
        for name,exe in methods[iteration%2:]+methods[:iteration%2]:
            command=[str(exe),*arguments]
            started=time.perf_counter()
            result=subprocess.run(command,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
            elapsed=time.perf_counter()-started
            text=result.stdout.decode(errors="replace")
            (root/f"{name}-{iteration}.log").write_text(text,encoding="utf-8")
            assert result.returncode==0,(name,text)
            # Banners, executable paths and elapsed seconds legitimately differ.
            # Compare the complete size/statistics block printed after loading.
            block=text.split(str(source),1)[1]
            block=re.sub(r"\s*\d+ seconds elapsed\s*$","",block).strip()
            if output_contract is None: output_contract=block
            assert block==output_contract,f"{name}: native validation/statistics output changed"
            samples.append({"implementation":name,"iteration":iteration,"warmup":iteration==0,"seconds":elapsed})
            print(name,iteration,f"{elapsed:.5f}s",flush=True)
    assert source.read_bytes()==data
    if original: assert original.read_bytes()==data
    for name,exe in methods: assert digest(exe.read_bytes())==hashes[name],"Binary changed during benchmark"
    summaries=[]
    for name,_ in methods:
        times=[r["seconds"] for r in samples if r["implementation"]==name and not r["warmup"]]
        summaries.append({"implementation":name,"seconds":times,"median_seconds":statistics.median(times)})
    report={"schema_version":1,"kind":"whole_command_native_index_validation","timestamp_utc":datetime.now(timezone.utc).isoformat(),
        "fixture":"private_native_map" if original else "generated_overlapping_surface_index_ranges","game":args.game,"counts":counts,
        "platform":platform.platform(),"processor":platform.processor(),"logical_cpus":os.cpu_count(),"input_bytes":len(data),
        "input_sha256":digest(data),"compiler_sha256":hashes,"arguments":arguments,"samples":samples,"summaries":summaries,
        "median_speedup":summaries[0]["median_seconds"]/summaries[1]["median_seconds"],"native_statistics_identical":True,"input_unchanged":True}
    (root/"benchmark.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(summaries,indent=2))


if __name__=="__main__":
    main()

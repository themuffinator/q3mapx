"""Large-portal clipping and winding-order controls. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

from fixtures import create_fixture
from integration import Bsp, run
from vis_merge import portal, prt, rectangle, rows
from vis_fixtures import create_round_vis_fixture


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--reference',type=Path,help='Optional preceding executable; differences are recorded, not accepted as an oracle')
    parser.add_argument('--work-dir',type=Path,required=True)
    args=parser.parse_args()
    exe,root=args.compiler.resolve(),args.work_dir.resolve()
    source=create_fixture(root,patch=False)
    base=['-game','quake3','-fs_basepath',root,'-fs_homepath',root/'home']
    run(exe,[*base,'-threads',1,'-meta',source],root,'bsp')
    carrier=Bsp(source.with_suffix('.bsp')); data=bytearray(carrier.data)
    offset,length=carrier.lumps[4]
    for at in range(offset,offset+length,48):
        if struct.unpack_from('<i',data,at)[0]>=0: struct.pack_into('<i',data,at,0)
    # The BSP supplies valid native storage; these synthetic graphs isolate the
    # portal solver and do not claim to be spatially matched to the carrier.
    target=root/'clipping.bsp'; target.write_bytes(data); original=Bsp(target)
    records=[]
    for count in (24,25,32,64,129,512):
        for blocked in (False,True):
            center=128 if blocked else 56
            # Every open chain has the common axial line y=z=0 through all
            # three convex openings. In the blocked chain, any ray through the
            # first two squares reaches x=128 at |y|,|z|<=12, but the last
            # polygon has y>=64. Thus only the first/last cluster pair is hidden.
            wanted=[7,15,15,14] if blocked else [15]*4
            for shift in (0,count//2):
                points=[(128,center+64*math.cos(2*math.pi*(i+shift)/count),
                         64*math.sin(2*math.pi*(i+shift)/count)) for i in range(count)]
                openings=[rectangle(0,-4,4,-4,4),rectangle(64,-4,4,-4,4),points]
                for reverse in (False,True):
                    text=prt(4,[portal(i+1,i,list(reversed(p))) if reverse else portal(i,i+1,p)
                                for i,p in enumerate(openings)])
                    for options in ([],['-passageOnly'],['-nopassage']):
                        expected=None
                        for workers in (1,4):
                            label=f'{count}-{blocked}-{shift}-{reverse}-{len(records)}'
                            target.write_bytes(data); target.with_suffix('.prt').write_text(text)
                            run(exe,[*base,'-threads',workers,'-vis','-reproducible','-saveprt',*options,target],root,label,timeout=30)
                            output=Bsp(target); matrix=rows(output)
                            assert matrix==wanted,(label,options,matrix,wanted)
                            if expected is None: expected=output.lump(16)
                            assert output.lump(16)==expected,(label,'worker result differs')
                            for lump in range(1,16): assert output.lump(lump)==original.lump(lump),(label,lump)
                            assert target.with_suffix('.prt').read_text()==text
                            assert 'over-capacity cuts' not in (root/(label+'.log')).read_text()
                            record={'points':count,'blocked':blocked,'shift':shift,'reverse':reverse,
                                'options':options,'workers':workers,'rows':matrix,
                                'prt_sha256_lf':hashlib.sha256(text.encode()).hexdigest(),
                                'visibility_sha256':hashlib.sha256(output.lump(16)).hexdigest()}
                            if args.reference and workers==1:
                                target.write_bytes(data)
                                reference=subprocess.run([str(args.reference.resolve()),*map(str,base),'-threads','1',
                                    '-vis','-reproducible','-saveprt',*options,str(target)],cwd=root,capture_output=True,timeout=30)
                                log=reference.stdout+reference.stderr
                                (root/(label+'-reference.log')).write_bytes(log)
                                assert reference.returncode in (0,1) and (not reference.returncode or b'MAX_SEPERATORS' in log)
                                record['reference']={'exit_code':reference.returncode,
                                    'rows':None if reference.returncode else rows(Bsp(target))}
                                if reference.returncode: assert target.read_bytes()==data
                            records.append(record)
    # A real matched BSP/PRT pair complements the synthetic analytic controls.
    # The unobstructed straight corridor shares a central axial sight line.
    native=root/'native'
    source=create_round_vis_fixture(native)
    native_base=['-game','quake3','-fs_basepath',native,'-fs_homepath',native/'home']
    run(exe,[*native_base,'-threads',1,'-meta',source],native,'bsp')
    original=Bsp(source.with_suffix('.bsp')); text=source.with_suffix('.prt').read_text()
    lines=text.splitlines(); clusters,portals=map(int,lines[1:3])
    assert clusters==5 and portals==4 and all(int(line.split()[0])==64 for line in lines[4:4+portals])
    assert not source.with_suffix('.lin').exists(),'Round corridor leaked'
    native_records=[]
    for options in ([],['-passageOnly'],['-nopassage']):
        for workers in (1,4):
            label=f'native-{len(native_records)}'
            source.with_suffix('.bsp').write_bytes(original.data)
            run(exe,[*native_base,'-threads',workers,'-vis','-reproducible','-saveprt',*options,source],native,label)
            output=Bsp(source.with_suffix('.bsp'))
            assert rows(output)==[(1<<clusters)-1]*clusters,(label,'Axial visibility was lost')
            for lump in range(1,16): assert output.lump(lump)==original.lump(lump),(label,lump)
            assert source.with_suffix('.prt').read_text()==text
            assert 'over-capacity cuts' not in (native/(label+'.log')).read_text()
            native_records.append({'options':options,'workers':workers,'rows':rows(output),
                'visibility_sha256':hashlib.sha256(output.lump(16)).hexdigest()})
    assert not list(root.rglob('*.q3mapx-*.tmp'))
    report={'schema_version':1,'compiler_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
        'reference_sha256':hashlib.sha256(args.reference.read_bytes()).hexdigest() if args.reference else None,
        'checks':records,'native':{'source_sha256_lf':hashlib.sha256(source.read_text().encode()).hexdigest(),
            'bsp_before_vis_sha256':hashlib.sha256(original.data).hexdigest(),
            'prt_sha256_lf':hashlib.sha256(text.encode()).hexdigest(),
            'clusters':clusters,'portals':portals,'portal_points':64,'checks':native_records},'result':'passed'}
    (root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'{len(records)} analytic large-portal controls and {len(native_records)} matched corridor builds passed; rotated/reversed windings and 1/4 workers')


if __name__=='__main__': main()

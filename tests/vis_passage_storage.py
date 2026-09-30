"""Maximum passage-descriptor and word-span controls. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from fixtures import create_fixture
from integration import Bsp, run
from vis_merge import passage_storage, portal, prt, rectangle, rows


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
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
    # Synthetic PRTs isolate graph/storage contracts; the carrier BSP is not a
    # claim of matching spatial geometry. All native payloads except VIS/entities
    # must remain unchanged, including the remapped carrier leaf lump.
    target=root/'storage.bsp'; target.write_bytes(data); original=Bsp(target)
    records=[]
    for name,clusters,openings in [
        ('max-degree-empty',1025,[portal(0,i+1,rectangle(0)) for i in range(1024)]),
        ('wide-spans',131,[portal(i,i+1,rectangle(i*8)) for i in range(130)])]:
        text=prt(clusters,openings); expected=None
        wanted=[(1<<clusters)-1]*clusters if name=='wide-spans' else [(1<<clusters)-1]+[1|(1<<i) for i in range(1,clusters)]
        for options in ([],['-passageOnly']):
            for workers in (1,4):
                target.write_bytes(data); target.with_suffix('.prt').write_text(text)
                label=f'{name}-{len(options)}-{workers}'
                run(exe,[*base,'-threads',workers,'-vis','-reproducible','-saveprt',*options,target],root,label,timeout=120)
                output=Bsp(target); matrix=rows(output)
                assert matrix==wanted,(label,'independent PVS expectation differs')
                if expected is None: expected=output.lump(16)
                assert output.lump(16)==expected,(label,'solver/worker output differs')
                for lump in range(1,16): assert output.lump(lump)==original.lump(lump),(label,lump)
                assert target.with_suffix('.prt').read_text()==text
                live=len(openings)*2; width=((live+63)//64)*8
                storage=passage_storage((root/(label+'.log')).read_text(),options,live,width)
                if name=='max-degree-empty':
                    assert storage['passages']==1024*1024+1024
                    assert storage['empty_masks']==storage['passages'] and storage['candidate_visits']==0
                    assert storage['retained_bytes']==8*storage['passages'] and storage['blocks']==live
                else:
                    assert 0<storage['empty_masks']<storage['passages'] and storage['candidate_visits']>0
                    assert storage['retained_bytes']<storage['passages']*(8+width)
                records.append({'case':name,'options':options,'workers':workers,'storage':storage,
                    'visibility_sha256':hashlib.sha256(output.lump(16)).hexdigest()})
    assert not list(root.rglob('*.q3mapx-*.tmp'))
    (root/'validation.json').write_text(json.dumps({'schema_version':1,
        'compiler_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'checks':records,'result':'passed'},indent=2)+'\n')
    print('8 maximum-descriptor and multiword-span controls passed with independent PVS expectations')


if __name__=='__main__': main()

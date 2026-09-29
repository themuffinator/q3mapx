"""Worker-independent VIS on a fixture that exposed NRC scheduling differences. GPL-3.0-or-later."""
import argparse
import hashlib
import json
from pathlib import Path
from fixtures import create_fixture
from integration import Bsp,run

parser=argparse.ArgumentParser()
parser.add_argument('--compiler',type=Path,required=True)
parser.add_argument('--work-dir',type=Path,required=True)
args=parser.parse_args()
root,exe=args.work_dir.resolve(),args.compiler.resolve()
source=create_fixture(root,dense=True,grid=9)
base=['-game','quake3','-fs_basepath',root]
run(exe,[*base,'-threads',1,'-meta',source],root,'bsp')
original=source.with_suffix('.bsp').read_bytes()
records=[]
for mode in ([],['-nopassage'],['-passageOnly']):
    expected=None
    for workers in (1,4,20,70,20):
        source.with_suffix('.bsp').write_bytes(original)
        result=run(exe,[*base,'-threads',workers,'-vis','-reproducible','-saveprt',*mode,source],
                   root,f'vis-{len(records)}',timeout=120)
        visibility=Bsp(source.with_suffix('.bsp')).lump(16)
        if expected is None: expected=visibility
        assert visibility==expected,(mode,workers)
        records.append({'mode':mode or ['default'],'workers':workers,'seconds':result['seconds'],
                        'visibility_sha256':hashlib.sha256(visibility).hexdigest()})
(root/'report.json').write_text(json.dumps(records,indent=2)+'\n')
print('All three precise VIS algorithms reproduce bytes at 1/4/20/70 workers and repeated runs')

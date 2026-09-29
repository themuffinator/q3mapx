"""Exercise a portable release with development DLL paths removed. GPL-3.0-or-later."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--package-dir',type=Path,required=True)
parser.add_argument('--work-dir',type=Path,required=True)
parser.add_argument('--workbench-test',type=Path)
args=parser.parse_args()
package,root=args.package_dir.resolve(),args.work_dir.resolve()
root.mkdir(parents=True,exist_ok=True)
manifest=json.loads((package/'runtime-manifest.json').read_text(encoding='utf-8'))
for entry in manifest['files']:
    path=package/entry['path']
    assert hashlib.sha256(path.read_bytes()).hexdigest()==entry['sha256'],f'Hash mismatch: {path}'
assert hashlib.sha256((package/'q3mapx-source.zip').read_bytes()).hexdigest()==manifest['source_archive_sha256'],'Source archive hash mismatch'
env=os.environ.copy()
system=Path(os.environ['SystemRoot'])
env['PATH']=os.pathsep.join(map(str,[package/'bin',system/'System32',system]))
for key in list(env):
    if key.startswith(('QT_','QML')): env.pop(key)
compiler=package/'bin/q3mapx.exe'
checks=[]

def run(command,label,timeout=180,extra=None):
    result=subprocess.run(list(map(str,command)),cwd=root,env=env | (extra or {}),capture_output=True,timeout=timeout)
    (root/(label+'.log')).write_bytes(result.stdout+result.stderr)
    assert result.returncode==0,(label,result.returncode,(result.stdout+result.stderr)[-8000:])
    checks.append(label)
    print(label+' passed',flush=True)
    return result

version=run([compiler,'--version'],'version').stdout.decode('utf-8').strip()
if 'version' in manifest:
    assert version.startswith('q3mapx '+manifest['version']+' '),version
for name in ('integration','decompile','minimap','gpu','lighting','lighting_gpu',
             'game_profiles','native_fakk','native_mohaa','native_early','mesh_export','bsp_inspect','raven_compat','lightgrid_cli','portal_validation'):
    run([sys.executable,ROOT/'tests'/f'{name}.py','--compiler',compiler,'--work-dir',root/name],name)
gui=package/'bin/q3mapx-workbench.exe'
if gui.is_file():
    if args.workbench_test:
        run([sys.executable,ROOT/'tests/workbench.py','--test',args.workbench_test.resolve(),
             '--compiler',compiler,'--work-dir',root/'workbench'],'workbench')
        project=root/'workbench/project.q3mapx.json'
    else:
        project=root/'project.q3mapx.json'
        project.write_text(json.dumps({'schema_version':1,'name':'Portable release check','compiler':str(compiler),
            'source':str(root/'integration/pipeline-1/baseq3/maps/fixture.map'),
            'game_root':str(root/'integration/pipeline-1'),'output_root':str(root/'outputs'),'reproducible_vis':True}),encoding='utf-8')
    result=run([gui,'-platform','offscreen','--project',project,'--state-dir',root/'ui-state',
                '--render-preview',root/'workbench.png'],'preview',extra={'QT_DEBUG_PLUGINS':'1','QT_FORCE_STDERR_LOGGING':'1'})
    assert (root/'workbench.png').stat().st_size>10000,'Preview was not rendered'
    loaded=[]
    for line in result.stderr.decode('utf-8',errors='replace').splitlines():
        if re.search(r'\bloaded library\b',line):
            loaded.append(line)
            assert not any(prefix in line.lower() for prefix in ('/mingw64/','/ucrt64/','\\mingw64\\','\\ucrt64\\')),line
    assert any('qoffscreen' in line for line in loaded),'Qt did not report loading the packaged platform plugin'
(root/'validation.json').write_text(json.dumps({'schema_version':1,'package':str(package),'revision':manifest['revision'],
    'compiler_version':version,'restricted_path':env['PATH'],'runtime_hashes_verified':len(manifest['files']),
    'source_archive_hash_verified':True,'checks':checks,'result':'passed'},indent=2)+'\n')
print('Portable release passed without development DLL directories on PATH')

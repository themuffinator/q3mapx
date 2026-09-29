"""Malformed PRT counts, indices and geometry must fail before traversal. GPL-3.0-or-later."""
import argparse
import math
from pathlib import Path
import re
import subprocess
from fixtures import create_fixture
from integration import run

parser=argparse.ArgumentParser()
parser.add_argument('--compiler',type=Path,required=True)
parser.add_argument('--work-dir',type=Path,required=True)
args=parser.parse_args()
exe,root=args.compiler.resolve(),args.work_dir.resolve()
source=create_fixture(root,dense=True,grid=5)
base=['-game','quake3','-fs_basepath',root,'-threads',4]
run(exe,[*base,'-meta',source],root,'bsp')
original=source.with_suffix('.prt').read_text()
lines=original.splitlines()
clusters=int(lines[1]); ports=int(lines[2])
assert ports>0
cases={}
for name,index,value in [('negative-clusters',1,'-1'),('huge-clusters',1,'999999999999999'),
                         ('negative-portals',2,'-1'),('bitset-overflow',2,'65537'),('negative-faces',3,'-1')]:
    changed=lines.copy(); changed[index]=value; cases[name]='\n'.join(changed)+'\n'
for name,index,value in [('negative-points',0,'-1'),('few-points',0,'2'),('many-points',0,'513'),
                         ('negative-leaf',1,'-1'),('leaf-off-by-one',2,str(clusters)),('huge-index',1,'999999999999999999')]:
    changed=lines.copy(); tokens=changed[4].split(); tokens[index]=value; changed[4]=' '.join(tokens); cases[name]='\n'.join(changed)+'\n'
for name,value in [('nan','nan'),('infinity','inf'),('overflow','1e999')]:
    changed=lines.copy(); start=changed[4].index('(')+1; end=changed[4].index(' ',start)
    changed[4]=changed[4][:start]+value+changed[4][end:]; cases[name]='\n'.join(changed)+'\n'
cases['degenerate']=f'PRT1\n{clusters}\n1\n0\n3 0 1 0 (0 0 0) (0 0 0) (0 0 0)\n'
original_bsp=source.with_suffix('.bsp').read_bytes()
bad=root/'bad.bsp'; bad.write_bytes(original_bsp)
for name,text in cases.items():
    bad.with_suffix('.prt').write_text(text)
    for force in ([],['-force']):
        result=subprocess.run([str(exe),*map(str,base),*force,'-vis','-saveprt',str(bad)],cwd=root,capture_output=True,timeout=15)
        (root/f'{name}-{bool(force)}.log').write_bytes(result.stdout+result.stderr)
        assert result.returncode!=0 and b'ERROR' in result.stdout, (name,result.returncode,result.stdout[-2000:])
        assert bad.read_bytes()==original_bsp, name
print(f'{len(cases)*2} portal safety checks passed; original BSP preserved')

# Exercise the documented 512-point input limit, beyond the old 128-entry
# clipping scratch arrays. Keep a convex polygon inside the first portal.
points=[tuple(map(float,p.split())) for p in re.findall(r'\(([^)]+)\)',lines[4])]
center=tuple(sum(p[i] for p in points)/len(points) for i in range(3))
u=tuple(points[1][i]-points[0][i] for i in range(3))
v=tuple(points[2][i]-points[0][i] for i in range(3))
norm=lambda p: math.sqrt(sum(x*x for x in p))
u=tuple(x/norm(u) for x in u)
dot=sum(u[i]*v[i] for i in range(3))
v=tuple(v[i]-dot*u[i] for i in range(3))
v=tuple(x/norm(v) for x in v)
radius=min(norm(tuple(p[i]-center[i] for i in range(3))) for p in points)*0.1
for count in (129,512):
    changed=lines.copy()
    polygon=[tuple(center[j]+radius*(math.cos(2*math.pi*i/count)*u[j]+math.sin(2*math.pi*i/count)*v[j]) for j in range(3)) for i in range(count)]
    changed[4]=' '.join([str(count),*lines[4].split()[1:4]])+' '+ ' '.join('(%0.9g %0.9g %0.9g)'%p for p in polygon)
    bad.with_suffix('.prt').write_text('\n'.join(changed)+'\n')
    bad.write_bytes(original_bsp)
    result=subprocess.run([str(exe),*map(str,base),'-vis','-nopassage','-saveprt',str(bad)],cwd=root,capture_output=True,timeout=30)
    (root/f'large-winding-{count}.log').write_bytes(result.stdout+result.stderr)
    # Very complex convex windings can exceed the inherited separator cache.
    # A bounded diagnostic is acceptable; access violations and other failures are not.
    assert result.returncode==0 or (result.returncode==1 and b'MAX_SEPERATORS' in result.stdout),result.stdout[-2000:]
print('129- and 512-point portal inputs finish or report the separator limit safely')

"""Balanced block trees, independent cell geometry and depth-guard preservation.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from fixtures import create_fixture, box
from integration import run
from patch_input import payloads
from patch_paint import normalized
from patch_color_recovery import pack_bsp, read_surfaces
from vis_fixtures import create_subdivided_corridor


def tree(data):
    lumps=payloads(data)
    nodes=list(struct.iter_unpack('<9i',lumps[3]))
    planes=list(struct.iter_unpack('<4f',lumps[2]))
    leaves=list(struct.iter_unpack('<12i',lumps[4]))
    cells={}; maximum=0
    stack=[(0,(-1e8,)*3,(1e8,)*3,0)]
    while stack:
        index,lo,hi,depth=stack.pop()
        if index<0:
            cluster=leaves[-index-1][0]
            if cluster>=0:
                assert cluster not in cells
                cells[cluster]=(lo,hi)
            continue
        maximum=max(maximum,depth+1)
        plane=planes[nodes[index][0]]
        axis=next(i for i in range(3) if abs(plane[i])==1)
        assert sum(abs(x) for x in plane[:3])==1
        dist=plane[3]/plane[axis]
        for side,child in enumerate(nodes[index][1:3]):
            lower=list(lo); upper=list(hi)
            if (side==0)==(plane[axis]>0): lower[axis]=max(lower[axis],dist)
            else: upper[axis]=min(upper[axis],dist)
            stack.append((child,tuple(lower),tuple(upper),depth+1))
    return maximum,cells


def transform(source,axis,offset):
    def point(values):
        x,y,z=map(float,values); x+=offset
        return ((x,y,z),(z,x,y),(y,z,x))[axis]
    text=source.read_text()
    text=re.sub(r'\( ([^()]+) \)',lambda m:'( '+' '.join(format(v,'.9g') for v in point(m[1].split()))+' )',text)
    text=text.replace('"origin" "20 64 64"','"origin" "'+' '.join(map(str,point((20,64,64))))+'"')
    sizes=[1024]*3; sizes[axis]=8
    text=text.replace('"8 1024 1024"','"'+' '.join(map(str,sizes))+'"')
    source.write_text(text)
    return point


def mesh(data):
    lumps=payloads(data); raven=data[:4]==b'RBSP'; stride,vertex,normal=(148,80,52) if raven else (104,44,28)
    result=[]
    for s in read_surfaces(data):
        first_vertex=struct.unpack_from('<i',lumps[13],s['surface']*stride+12)[0]
        first,count=struct.unpack_from('<2i',lumps[13],s['surface']*stride+20)
        indices=struct.unpack_from('<'+str(count)+'i',lumps[11],first*4)
        vertices=[(*xyz,*uv,*struct.unpack_from('<3f',lumps[10],(first_vertex+i)*vertex+normal))
                  for i,(xyz,uv,_) in enumerate(s['controls'])]
        for i in range(0,len(indices),3):
            face=[vertices[j] for j in indices[i:i+3]]
            result.append((s['model'],s['shader'],min(tuple(face[j:]+face[:j]) for j in range(3))))
    return sorted(result)


def partition_control(exe,reference,root):
    folder=root/'pillars'; source=create_subdivided_corridor(folder,blocks=128)
    pillars=[((48+i*112,40,16),(72+i*112,88,80)) for i in range(8)]
    text=source.read_text(); marker='}\n{\n"classname" "info_player_deathmatch"'
    source.write_text(text.replace(marker,''.join(box(lo,hi) for lo,hi in pillars)+marker))
    base=['-game','quake3','-fs_basepath',folder,'-fs_homepath',folder/'home','-threads',1]
    snapshots=[]
    def inside(p,lo,hi): return all(a<=x<b for x,a,b in zip(p,lo,hi))
    points=[(20+8*x,y,z) for x in range(128) for y in (24,64) for z in (32,96)
            if not any(inside((20+8*x,y,z),lo,hi) for lo,hi in pillars)]
    for name,binary in [('current',exe),*([('reference',reference.resolve())] if reference else [])]:
        run(binary,[*base,'-meta',source],folder,name+'-bsp',timeout=180)
        raw=source.with_suffix('.bsp').read_bytes(); depth,cells=tree(raw)
        volume=0
        for lo,hi in cells.values():
            assert all(a<b for a,b in zip(lo,hi))
            assert all(a>=lower and b<=upper for a,b,lower,upper in zip(lo,hi,(16,16,16),(1040,112,112)))
            assert not any(all(max(a,c)<min(b,d) for a,b,c,d in zip(lo,hi,other_lo,other_hi)) for other_lo,other_hi in pillars)
            volume+=(hi[0]-lo[0])*(hi[1]-lo[1])*(hi[2]-lo[2])
        assert volume==1024*96*96-8*24*48*64
        owners=[]
        for point in points:
            owners.append(next(c for c,(lo,hi) in cells.items() if inside(point,lo,hi)))
        run(binary,[*base,'-vis','-saveprt','-reproducible',source],folder,name+'-vis',timeout=180)
        data=payloads(source.with_suffix('.bsp').read_bytes())[16]; _,width=struct.unpack_from('<2i',data)
        rows=[int.from_bytes(data[8+i*width:8+(i+1)*width],'little') for i in owners]
        visibility=bytes(bool(row&(1<<c)) for row in rows for c in owners)
        for a,point in enumerate(points):
            for b,other in enumerate(points):
                if (point[2]==other[2]==96) or (point[1]==other[1]==24):
                    assert visibility[a*len(points)+b],'A known clear segment was culled'
        snapshots.append((mesh(raw),visibility))
    if reference: assert snapshots[0]==snapshots[1],'Mapped visibility or rendered mesh changed'
    return dict(probe_points=len(points),mapped_visibility_pairs=len(points)**2,
                free_cell_volume_verified=True,reference_mesh_and_visibility_parity=bool(reference))


def generation_guard(exe,root):
    folder=root/'generation-guard'; source=create_subdivided_corridor(folder,blocks=1030)
    shader=source.parent.parent/'scripts/q3mapx_tests.shader'
    shader.write_text(shader.read_text()+'''\ntextures/q3mapx/skip
{
qer_editorimage textures/q3mapx/checker.tga
surfaceparm skip
surfaceparm nonsolid
surfaceparm nodraw
}
textures/q3mapx/hint
{
qer_editorimage textures/q3mapx/checker.tga
surfaceparm hint
surfaceparm structural
surfaceparm nonsolid
surfaceparm nodraw
}
''')
    geometry=''.join(box((20+i*8,16,16),(21+i*8,112,112),'q3mapx/skip').replace('q3mapx/skip','q3mapx/hint',1) for i in range(1025))
    marker='}\n{\n"classname" "info_player_deathmatch"'
    source.write_text(source.read_text().replace('"8 1024 1024"','"0 0 0"').replace(marker,geometry+marker))
    for suffix in ('.bsp','.srf'): source.with_suffix(suffix).write_bytes(b'previous output')
    result=subprocess.run([str(exe),'-game','quake3','-fs_basepath',str(folder),'-fs_homepath',str(folder/'home'),'-threads','1','-v','-meta',str(source)],cwd=folder,capture_output=True,timeout=180)
    log=result.stdout+result.stderr; (folder/'compile.log').write_bytes(log)
    assert result.returncode==1 and b'Cannot build BSP tree: node depth exceeds safety limit of 1024' in log
    assert re.search(rb'\b1061 faces\b',log),'Expected 1025 hint faces plus 36 enclosing brush faces'
    assert all(source.with_suffix(suffix).read_bytes()==b'previous output' for suffix in ('.bsp','.srf'))
    return dict(hint_planes=1025,controlled_failure=True,bsp_and_srf_preserved=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,required=True); p.add_argument('--work-dir',type=Path,required=True)
    p.add_argument('--reference',type=Path)
    p.add_argument('--generation-only',action='store_true',help='Repeat only the deep-hint publication guard')
    a=p.parse_args(); exe=a.compiler.resolve(); root=a.work_dir.resolve(); root.mkdir(parents=True,exist_ok=True)
    if a.generation_only:
        result=dict(generation_guard=generation_guard(exe,root),compiler_sha256=hashlib.sha256(exe.read_bytes()).hexdigest())
        (root/'generation-guard.json').write_text(json.dumps(result,indent=2)+'\n')
        print('1025 explicit hint planes rejected before BSP/SRF publication; 1061 structural faces verified')
        return
    cases=[]; guards=[]; ordinary=[]
    for game in ('quake3','ja'):
        for blocks,axis,offset in [(128,0,0),(1024,0,0),(1024,1,-4096),(1024,2,-4096),(4096,0,-16384)]:
            folder=root/f'{game}-{blocks}-{axis}'; source=create_subdivided_corridor(folder,blocks=blocks)
            point=transform(source,axis,offset)
            # JA uses another shader directory; these are independent copies.
            shaders=source.parent.parent/'shaders'; shaders.mkdir(exist_ok=True)
            for file in (source.parent.parent/'scripts').iterdir(): (shaders/file.name).write_bytes(file.read_bytes())
            base=['-game',game,'-fs_basegame','baseq3','-fs_basepath',folder,'-fs_homepath',folder/'home']
            expected={(point((16+i*8,16,16)),point((24+i*8,112,112))) for i in range(blocks)}
            old=None
            if a.reference and blocks<=1024 and axis==0:
                run(a.reference.resolve(),[*base,'-threads',1,'-meta',source],folder,'reference-bsp',timeout=300)
                old=source.with_suffix('.bsp').read_bytes(); old_depth,old_cells=tree(old)
                assert set(old_cells.values())==expected
                (folder/'reference.bsp').write_bytes(old)
            snapshots=[]
            for workers in ((1,4) if axis==0 and blocks<=1024 else (1,)):
                run(exe,[*base,'-threads',workers,'-v','-meta',source],folder,f'bsp-{workers}',timeout=300)
                assert not source.with_suffix('.lin').exists()
                raw=source.with_suffix('.bsp').read_bytes(); depth,cells=tree(raw)
                if old: assert mesh(raw)==mesh(old),'Corridor rendered triangle samples changed'
                assert set(cells.values())==expected and len(cells)==blocks
                assert depth<=blocks.bit_length()+6,(blocks,depth)
                data=payloads(raw)
                if snapshots: assert normalized(data)==normalized(snapshots[0]),'Worker-dependent BSP output'
                snapshots.append(data)
                # The room is unobstructed: every interior cluster sees every other.
                run(exe,[*base,'-threads',workers,'-vis','-merge','-saveprt','-reproducible',source],folder,f'vis-{workers}',timeout=180)
                vis=payloads(source.with_suffix('.bsp').read_bytes())[16]
                count,width=struct.unpack_from('<2i',vis)
                assert count==blocks and len(vis)==8+count*width
                assert all(int.from_bytes(vis[8+i*width:8+(i+1)*width],'little')==(1<<count)-1 for i in range(count))
                if workers==1:
                    run(exe,[*base,'-threads',1,'-minimap','-size',16,'-samples',1,'-o',folder/'minimap.tga',source],folder,'minimap')
                    if blocks==1024 and axis==0:
                        run(exe,[*base,'-threads',1,'-light','-fast',source],folder,'light',timeout=300)
                        assert tree(source.with_suffix('.bsp').read_bytes())[1]==cells
                cases.append(dict(game=game,blocks=blocks,axis=axis,offset=offset,workers=workers,depth=depth,
                    expected_cells=True,all_visible=True,reference_depth=old_depth if old else None,
                    input_sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
        for dense in (False,True):
            folder=root/f'{game}-ordinary-{dense}'; source=create_fixture(folder,dense=dense,grid=9)
            shaders=source.parent.parent/'shaders'; shaders.mkdir(exist_ok=True)
            for file in (source.parent.parent/'scripts').iterdir(): (shaders/file.name).write_bytes(file.read_bytes())
            base=['-game',game,'-fs_basegame','baseq3','-fs_basepath',folder,'-fs_homepath',folder/'home','-threads',1]
            run(exe,[*base,'-meta',source],folder,'current')
            current=source.with_suffix('.bsp').read_bytes(); prt=source.with_suffix('.prt').read_bytes()
            if a.reference:
                run(a.reference.resolve(),[*base,'-meta',source],folder,'reference')
                assert normalized(payloads(current))==normalized(payloads(source.with_suffix('.bsp').read_bytes()))
                assert source.with_suffix('.prt').read_bytes()==prt
            ordinary.append(dict(game=game,dense=dense,reference_native_lump_and_prt_parity=bool(a.reference)))

    # These synthetic node graphs test the reader's graph contract only, not
    # geometric BSP validity. Longest paths must include previously visited tails.
    folder=root/'reader'; source=create_fixture(folder,patch=False)
    base=['-game','quake3','-fs_basepath',folder,'-fs_homepath',folder/'home','-threads',1]
    run(exe,[*base,'-meta',source],folder,'carrier'); raw=source.with_suffix('.bsp').read_bytes()
    lumps=payloads(raw); template=list(struct.unpack_from('<9i',lumps[3]))
    for name,n in [('at-limit',1024),('forward',1025),('reverse',1025),('shared-tail',1025)]:
        nodes=[]
        for i in range(n):
            node=template.copy(); node[1]=i+1 if i+1<n else -1; node[2]=-1
            if name=='reverse': node[1]=i-1 if i else -1
            if name=='shared-tail' and i==0: node[1:3]=[600,1]
            nodes.append(struct.pack('<9i',*node))
        changed=list(lumps); changed[3]=b''.join(nodes)
        bsp=folder/(name+'.bsp'); bsp.write_bytes(pack_bsp(raw,changed))
        for force in (False,True):
            output=folder/'preserved.map'; report=folder/'preserved.json'
            output.write_bytes(b'old map'); report.write_bytes(b'old report')
            result=subprocess.run([str(exe),*map(str,[*base,*(['-force'] if force else []),'-decompile','-o',output,'-report',report,bsp])],cwd=folder,capture_output=True,timeout=60)
            log=result.stdout+result.stderr; (folder/f'{name}-{force}.log').write_bytes(log)
            if n==1024: assert result.returncode==0,log[-2000:]
            else:
                assert result.returncode==1 and b'node depth exceeds safety limit of 1024' in log,(name,log[-2000:])
                assert output.read_bytes()==b'old map' and report.read_bytes()==b'old report'
            guards.append(dict(case=name,force=force,accepted=n==1024))
    result=dict(cases=cases,ordinary=ordinary,guards=guards,
                occluded_partition=partition_control(exe,a.reference,root),
                generation_guard=generation_guard(exe,root),
                compiler_sha256=hashlib.sha256(exe.read_bytes()).hexdigest())
    (root/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(f'{len(cases)} generated cell/VIS cases, {len(ordinary)} ordinary controls, {len(guards)} native depth guards, occluded partition and generation publication guard passed')


if __name__=='__main__': main()

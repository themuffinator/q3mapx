# SPDX-License-Identifier: GPL-3.0-or-later
"""Geometry-sensitive OBJ/ASE checks, independent of compiler export internals."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess

from fixtures import create_fixture
from integration import Bsp, run


def close(a, b):
    assert len(a) == len(b) and max(abs(x-y) for x,y in zip(a,b)) < 2e-5, (a,b)


def obj_data(text):
    vertices, normals, uv, groups = [], [], [], {}
    group = None
    for line in text.splitlines():
        words = line.split()
        if not words:
            continue
        if words[0] == 'g':
            group = groups.setdefault(words[1], {'vertices': [], 'faces': []})
        if words[0] == 'v':
            vertex = tuple(map(float, words[1:])); vertices.append(vertex); group['vertices'].append(vertex)
        if words[0] == 'vn': normals.append(tuple(map(float,words[1:])))
        if words[0] == 'vt': uv.append(tuple(map(float,words[1:])))
        if words[0] == 'f':
            face = [tuple(int(i)-1 for i in word.split('/')) for word in words[1:]]
            assert len(face) == 3
            assert all(0<=v<len(vertices) and 0<=t<len(uv) and 0<=n<len(normals) for v,t,n in face)
            group['faces'].append(face)
    assert all(math.isfinite(v) for row in vertices+normals+uv for v in row)
    return vertices, normals, uv, groups


def bounds(vertices):
    return tuple(min(v[i] for v in vertices) for i in range(3)), tuple(max(v[i] for v in vertices) for i in range(3))


def repack(bsp, replacements):
    data = bytearray(b'IBSP'+struct.pack('<i',46)+bytes(17*8))
    for i in range(17):
        payload = replacements.get(i,bsp.lump(i))
        struct.pack_into('<ii',data,8+i*8,len(data),len(payload)); data.extend(payload)
    return data


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--work-dir',type=Path,required=True)
    args=parser.parse_args()
    exe, root=args.compiler.resolve(),args.work_dir.resolve()
    source=create_fixture(root)
    base=['-game','quake3','-fs_basepath',root,'-fs_homepath',root/'home']
    run(exe,[*base,'-threads',2,'-meta',source],root,'compile')
    path=source.with_suffix('.bsp'); bsp=Bsp(path); digest=hashlib.sha256(path.read_bytes()).digest()
    surfaces=[struct.unpack_from('<7i',bsp.lump(13),i) for i in range(0,len(bsp.lump(13)),104)]
    patch_id=next(i for i,s in enumerate(surfaces) if s[2]==2)
    planar_faces=sum(s[6]//3 for s in surfaces if s[2] in (1,3))
    outputs={}
    for steps in (2,8):
        for workers in (1,4):
            for fmt in ('obj','ase'):
                label=f'{fmt}-{steps}-{workers}'
                run(exe,[*base,'-threads',workers,'-convert','-format',fmt,'-patchsteps',steps,path],root,label)
                text=path.with_suffix('.'+fmt).read_text()
                assert not re.search(r'(?<!\w)[+-]?(nan|inf)(?!\w)',text,re.I)
                if workers==1: outputs[fmt,steps]=text
                else: assert text==outputs[fmt,steps], 'Worker scheduling changed mesh bytes'
                if fmt=='obj':
                    vertices,normals,uv,groups=obj_data(text)
                    assert 'mtllib fixture.mtl' in text and 'fixture.bsp.mtl' not in text
                    patch=next(g for name,g in groups.items() if name.endswith(f'model0surf{patch_id}'))
                    assert len(patch['vertices'])==(steps+1)**2 and len(patch['faces'])==steps*steps*2
                    assert sum(len(g['faces']) for g in groups.values())==planar_faces+steps*steps*2
                    center=vertices.index((-128.0,52.0,128.0)) # analytic quadratic center, not the 112-unit control point
                    close(normals[center],(0,1,0)); close(uv[center],(0,1)) # compiler removes an integer UV offset
                    door=[v for name,g in groups.items() if 'model1surf' in name for v in g['vertices']]
                    assert bounds(door)==((96,16,-48),(128,112,48)),bounds(door)
                    for face in patch['faces']:
                        a,b,c=(vertices[f[0]] for f in face)
                        ab=[b[i]-a[i] for i in range(3)]; ac=[c[i]-a[i] for i in range(3)]
                        cross=(ab[1]*ac[2]-ab[2]*ac[1],ab[2]*ac[0]-ab[0]*ac[2],ab[0]*ac[1]-ab[1]*ac[0])
                        assert sum(cross[i]*normals[face[0][2]][i] for i in range(3))>0, 'Patch winding opposes its normals'
                else:
                    total=0; door=[]
                    for block in text.split('*GEOMOBJECT')[1:]:
                        model,surface=map(int,re.search(r'model(\d+)surf(\d+)',block).groups())
                        total+=int(re.search(r'\*MESH_NUMFACES\s+(\d+)',block)[1])
                        xyz=[tuple(map(float,m.groups()[1:])) for m in re.finditer(r'\*MESH_VERTEX\s+(\d+)\s+([-+\d.e]+)\s+([-+\d.e]+)\s+([-+\d.e]+)',block)]
                        if model==1: door.extend(xyz)
                        for m in re.finditer(r'\*MESH_VERTEXNORMAL\s+(\d+)\s+([-+\d.e]+)\s+([-+\d.e]+)\s+([-+\d.e]+)',block):
                            local=int(m[1]); actual=tuple(map(float,m.groups()[1:]))
                            if surface!=patch_id:
                                offset=(surfaces[surface][3]+local)*44+28
                                close(actual,struct.unpack_from('<3f',bsp.lump(10),offset))
                    assert total==planar_faces+steps*steps*2
                    assert bounds(door)==((96,-48,16),(128,48,112)),bounds(door)
    assert hashlib.sha256(path.read_bytes()).digest()==digest

    # Short map tokens and long shader names previously fed unchecked pointer
    # subtraction and a fixed 256-byte copy in external-lightmap lookup.
    external=root/'baseq3/scripts/q3map2_fixture.shader'
    external.write_text('x'*300+'\n{\n{\nmap a\n}\n}\ntextures/q3mapx/stone\n{\n{\nmap maps/fixture/lm_0007.tga\n}\n}\n')
    run(exe,[*base,'-threads',2,'-convert','-format','obj','-lightmapsastexcoord',path],root,'external-lightmap')
    assert 'usemtl lm_0007' in path.with_suffix('.obj').read_text()
    assert 'newmtl lm_0007' in path.with_suffix('.mtl').read_text()
    external.write_text('')
    # Sparse high lightmap IDs must not create billions of unused materials.
    high=bytearray(bsp.lump(13)); struct.pack_into('<i',high,28,2147483647)
    path.write_bytes(repack(bsp,{13:high}))
    run(exe,[*base,'-threads',2,'-convert','-format','obj','-lightmapsastexcoord',path],root,'sparse-lightmap')
    assert path.with_suffix('.mtl').stat().st_size<10000
    output=path.with_suffix('.obj'); output.write_text('keep existing OBJ')
    p=subprocess.run([str(exe),*map(str,base),'-convert','-format','obj','-deluxemapsastexcoord',str(path)],cwd=root,capture_output=True,timeout=20)
    assert p.returncode==1 and b'overflows' in p.stdout and output.read_text()=='keep existing OBJ'
    entities=bsp.lump(0).replace(b'"classname" "worldspawn"',b'"classname" "worldspawn"\n"origin" "nan 0 0"',1)
    path.write_bytes(repack(bsp,{0:entities}))
    p=subprocess.run([str(exe),*map(str,base),'-convert','-format','obj',str(path)],cwd=root,capture_output=True,timeout=20)
    assert p.returncode==1 and b'origin' in p.stdout and output.read_text()=='keep existing OBJ'
    path.write_bytes(bsp.data)
    for value in ('0','33','nan','2.5','2147483648'):
        output=path.with_suffix('.obj'); output.write_text('keep existing OBJ')
        p=subprocess.run([str(exe),*map(str,base),'-convert','-format','obj','-patchsteps',value,str(path)],cwd=root,capture_output=True,timeout=20)
        assert p.returncode==1 and output.read_text()=='keep existing OBJ',(value,p.stdout)
    # A tiny malicious BSP can repeatedly reference the same patch controls.
    # The expansion budget must be checked before allocating or truncating output.
    patch=bsp.lump(13)[patch_id*104:(patch_id+1)*104]
    path.write_bytes(repack(bsp,{13:bsp.lump(13)+patch*4000}))
    output=path.with_suffix('.obj'); output.write_text('keep existing OBJ')
    p=subprocess.run([str(exe),*map(str,base),'-convert','-format','obj','-patchsteps','32',str(path)],cwd=root,capture_output=True,timeout=20)
    assert p.returncode==1 and b'budget' in p.stdout and output.read_text()=='keep existing OBJ'
    path.write_bytes(bsp.data)
    (root/'validation.json').write_text(json.dumps({'schema_version':1,'compiler_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
        'fixture_sha256':digest.hex(),'patch_center':[-128,-128,52],'patch_steps':[2,8],'workers':[1,4],
        'formats':['obj','ase'],'result':'passed'},indent=2)+'\n')
    print('Curved meshes, origin placement, normals, materials, worker parity and output protection passed')


if __name__=='__main__': main()

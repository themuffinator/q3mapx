"""Native lighting observations: rational coverage, stored bytes and real bakes.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from fractions import Fraction as F
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from fixtures import create_fixture, create_lighting_fixture
from integration import Bsp, run
from native_fakk import native_file


def parts(data):
    count = 17 if data[:4] == b'IBSP' else 18
    return [data[start:start+length] for start, length in
            (struct.unpack_from('<2i', data, 8+i*8) for i in range(count))]


def pack(header, lumps):
    result = bytearray(header[:8]+bytes(8*len(lumps)))
    for i, lump in enumerate(lumps):
        struct.pack_into('<2i', result, 8+i*8, len(result), len(lump))
        result.extend(lump)
        result.extend(bytes(-len(result) % 4))
    return bytes(result)


def truth(u, v):
    return [3*u+2*v-4, -u+5*v+7, 2*u-v+9]


def synthetic(game='quake3', mode='square'):
    raven = game != 'quake3'
    size = 512 if game == 'qfusion' else 128
    header = (b'FBSP' if game == 'qfusion' else b'RBSP' if raven else b'IBSP')+struct.pack('<i', 1 if raven else 46)
    lumps = [b'']*(18 if raven else 17)
    lumps[0] = b'{\n"classname" "worldspawn"\n}\n\0'
    lumps[1] = b'textures/test/observation\0'.ljust(64, b'\0')+struct.pack('<2i', 0, 1)
    uvs = [(1.5,1.5),(5.5,1.5),(5.5,5.5),(1.5,5.5)]
    if mode == 'rotated': uvs = [(v, 8-u) for u,v in uvs]
    if mode == 'mirrored': uvs = [(8-u, v) for u,v in uvs]
    if mode == 'oblique': uvs = [(u+v/2, v+u/4) for u,v in uvs]
    if mode == 'outside': uvs = [(u-100, v-100) for u,v in uvs]
    if mode == 'wide': uvs = [(.5,.5),(127.5,.5),(127.5,127.5),(.5,127.5)]
    if mode == 'extreme': uvs = [(1e35,1e35),(2e35,1e35),(2e35,2e35),(1e35,2e35)]
    geometric_uvs = [(1.5,1.5),(5.5,1.5),(5.5,5.5),(1.5,5.5)] if mode == 'extreme' else uvs
    positions = [truth(u,v) for u,v in geometric_uvs]
    normals = [(0,0,2)]*4
    triangles = [(0,1,2),(0,2,3)]
    if mode in ('ambiguous_position','ambiguous_normal'):
        uvs *= 2
        positions += [[*p[:2], p[2]+(128 if mode == 'ambiguous_position' else 0)] for p in positions]
        normals += [(0,2,0) if mode == 'ambiguous_normal' else (0,0,2)]*4
        triangles += [(4,5,6),(4,6,7)]
    if mode == 'constant': uvs = [(1.5,1.5)]*4
    if mode == 'degenerate': positions = [(0,0,0)]*4
    if mode == 'zero_normal': normals = [(0,0,0)]*4
    if mode == 'patch':
        uvs = [(x+.5,y+.5) for y in range(3) for x in range(3)]
        positions = [truth(u,v) for u,v in uvs]; normals = [(0,0,2)]*9; triangles=[]
    vertices=[]
    for i, (uv, xyz, normal) in enumerate(zip(uvs, positions, normals)):
        base = struct.pack('<5f', *xyz, 0, 0)
        lightmap = struct.pack('<2f', *(v/size for v in uv))
        colors = bytes([17+i,37,83,i*17])
        if raven:
            base += lightmap*2+bytes(16)+struct.pack('<3f', *normal)+colors+bytes([9,8,7,6])+bytes(8)
        else: base += lightmap+struct.pack('<3f', *normal)+colors
        assert len(base) == (80 if raven else 44)
        vertices.append(base)
    lumps[10] = b''.join(vertices)
    lumps[11] = b''.join(struct.pack('<3i', *tri) for tri in triangles)
    page = -3 if mode == 'vertex_lit' else 90 if mode == 'missing_page' else 0
    surface = struct.pack('<7i',0,-1,2 if mode == 'patch' else 1,0,len(vertices),0,len(triangles)*3)
    if raven:
        surface += bytes([0,7,254,255])*2+struct.pack('<12i',page,2,-3,-3,*([0]*8))
    else: surface += struct.pack('<3i',page,0,0)
    surface += struct.pack('<2i12f2i',size,size,*([0]*12),3 if mode == 'patch' else 0,3 if mode == 'patch' else 0)
    assert len(surface) == (148 if raven else 104)
    # Four independent surfaces exercise concurrent ownership and ensure shared
    # vertex/page records never collapse different surface/model associations.
    lumps[13] = surface*4
    model = lambda first,count: struct.pack('<6f4i',0,0,0,64,64,128,first,count,0,0)
    lumps[7] = model(0,3)+model(3,1)
    if mode == 'overlap': lumps[7] = model(0,4)+model(3,1)
    if mode == 'unowned': lumps[7] = model(0,2)+model(3,1)
    atlas = bytes(v for y in range(size) for x in range(size) for v in (x%256,y%256,(x+3*y)%256))
    # Page one is unreferenced (as with interleaved deluxe data). Never emit it.
    lumps[14] = atlas+bytes([255])*(size*size*3)+(bytes([255,42,0])*(size*size) if raven else b'')
    if mode == 'partial_page': lumps[14] += b'\x7f'
    if mode == 'absent': lumps[14] = b''
    if raven:
        dictionary = [bytes([i+1,2,3, 4,5,6, 0,0,0, 0,0,0,
                             11,12,13, 14,15,16, 0,0,0, 0,0,0, 0,7,254,255, 128,255]) for i in range(3)]
        indices = [2,1,0,1,2,0,2,1]
        lumps[15] = b''.join(dictionary); lumps[17] = struct.pack('<8H',*indices)
        grid = [dictionary[i] for i in indices]
    else:
        grid = [bytes([i+1,2,3,11,12,13,128,255]) for i in range(8)]
        lumps[15] = b''.join(grid)
    return pack(header,lumps), uvs, triangles, grid


def exact_coverage(uvs, triangles, size, stride=1):
    """Independent exact-rational half-plane membership, including shared edges."""
    result = {}
    orient = lambda a,b,p: (b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0])
    points = [(F(u),F(v)) for u,v in uvs]
    for y in range(0,min(size,16),stride):
        for x in range(0,min(size,16),stride):
            p = (F(2*x+1,2),F(2*y+1,2)); hits=[]
            for ids in triangles:
                a,b,c = [points[i] for i in ids]
                sides = [orient(a,b,p),orient(b,c,p),orient(c,a,p)]
                if all(s>=0 for s in sides) or all(s<=0 for s in sides): hits.append(any(s==0 for s in sides))
            if hits: result[x,y] = (len(hits),any(hits))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--work-dir',type=Path,required=True)
    args=parser.parse_args(); exe=args.compiler.resolve(); root=args.work_dir.resolve()
    root.mkdir(parents=True,exist_ok=True)
    common=['-fs_basepath',root,'-fs_homepath',root/'home','-fs_basegame','baseq3']
    summaries=[]
    failures=[]

    def analyze(path,label,game='quake3',options=(),threads=4):
        before=path.read_bytes(); output=root/f'{label}.json'
        run(exe,['-game',game,*common,'-threads',threads,'-bsp-evidence','-lighting','-report',output,*options,path],root,label)
        report=json.loads(output.read_text(encoding='utf-8'))
        assert path.read_bytes()==before
        assert report['source']['sha256']==hashlib.sha256(before).hexdigest()
        assert not report['source']['external_assets_loaded']
        assert not report['baked_lighting']['light_inference_performed']
        summaries.append({'case':label,'observations':report['baked_lighting']['sampling']['observations']})
        return report

    def fails(path,label,options,expected):
        output=root/'preserved.json'; output.write_text('existing report\n')
        before=path.read_bytes()
        command=[exe,'-game','quake3',*common,'-threads',4,'-bsp-evidence','-report',output,*options,path]
        result=subprocess.run(list(map(str,command)),cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=45)
        log=result.stdout.decode(errors='replace'); (root/f'{label}.log').write_text(log,encoding='utf-8')
        assert result.returncode==1 and expected in log,(label,log)
        assert output.read_text()=='existing report\n' and path.read_bytes()==before
        assert not list(root.glob('*.q3mapx-*'))
        failures.append(label)

    for game in ('quake3','ja','qfusion'):
        raven=game!='quake3'; size=512 if game=='qfusion' else 128
        for mode in ('square','rotated','mirrored','oblique'):
            native,uvs,triangles,grid=synthetic(game,mode)
            path=root/f'{game}-{mode}.bsp'; path.write_bytes(native)
            for stride in (1,3):
                label=f'{game}-{mode}-{stride}'
                report=analyze(path,label,game,['-lighting-stride',stride]); data=report['baked_lighting']
                assert report==analyze(path,label+'-one-worker',game,['-lighting-stride',stride],1)
                expected=exact_coverage(uvs,triangles,size,stride)
                assert expected
                for i,surface in enumerate(data['surfaces']):
                    assert surface['model']==int(i==3)
                    assert surface['coordinate_space']==('model_local_untransformed' if i==3 else 'world')
                    for slot in (0,1) if raven else (0,):
                        samples=surface['lightmap_slots'][slot]['observations']
                        assert {tuple(s['texel']) for s in samples}==expected.keys()
                        for sample in samples:
                            x,y=sample['texel']; hits,boundary=expected[x,y]
                            assert sample['triangle_hits']==hits and sample['triangle_boundary']==boundary
                            assert not sample['ambiguous_mapping']
                            assert max(abs(a-b) for a,b in zip(sample['position'],truth(x+.5,y+.5)))<1e-9
                            assert sample['normal']==[0,0,1]
                            assert sample['rgb']==([255,42,0] if slot else [x,y,(x+3*y)%256])
                            assert sample['has_255_channel']==bool(slot)
                    for vertex in surface['vertex_observations']:
                        index=vertex['vertex']; slot=vertex['slot']
                        assert index%stride==0 and vertex['style']==(7 if slot else 0)
                        assert vertex['rgba']==([9,8,7,6] if slot else [17+index,37,83,index*17])
                assert data['atlas']['referenced_pages']==(2 if raven else 1)
                assert data['grid']['status']=='conventional_layout_count_matches'
                assert data['grid']['dimensions']==[2,2,2]
                for observation in data['grid']['observations']:
                    i=observation['record']; slot=observation['slot']; original=grid[i]
                    assert i%stride==0
                    assert observation['conventional_position']==[(i%2)*64,((i//2)%2)*64,(i//4)*128]
                    assert observation['ambient_rgb']==list(original[slot*3:slot*3+3])
                    start=(12 if raven else 3)+slot*3
                    assert observation['directed_rgb']==list(original[start:start+3])
                    assert observation['latlong_bytes']==[128,255]

    for mode in ('constant','degenerate','ambiguous_position','ambiguous_normal','zero_normal','outside','extreme',
                 'patch','missing_page','vertex_lit','partial_page','absent','overlap','unowned'):
        native,_,_,_=synthetic(mode=mode); path=root/f'{mode}.bsp'; path.write_bytes(native)
        report=analyze(path,mode)
        assert report==analyze(path,mode+'-one-worker',threads=1)
        data=report['baked_lighting']; surface=data['surfaces'][0]; slot=surface['lightmap_slots'][0]
        if mode.startswith('ambiguous'):
            assert len(slot['observations'])==25
            assert all(s['ambiguous_mapping'] and s['position'] is None and s['normal'] is None for s in slot['observations'])
        elif mode=='zero_normal': assert all(s['normal'] is None for s in slot['observations'])
        elif mode=='overlap': assert data['surfaces'][3]['model_ownership']=='overlapping'
        elif mode=='unowned': assert data['surfaces'][2]['model_ownership']=='unowned'
        else: assert not slot['observations'],mode
        if mode=='constant': assert slot['degenerate_or_ill_conditioned_uv_triangles']==2
        if mode=='extreme': assert slot['degenerate_geometry_triangles']==0 and slot['degenerate_or_ill_conditioned_uv_triangles']==0
        if mode=='degenerate': assert slot['degenerate_geometry_triangles']==2
        if mode=='patch': assert slot['status']=='patch_parameterization_pending' and surface['vertex_role']=='bezier_control'
        if mode in ('partial_page','missing_page','absent'): assert slot['status']=='invalid_or_unavailable_page'
        if mode=='vertex_lit': assert slot['status']=='no_internal_page'

    native,_,_,_=synthetic(); path=root/'limits.bsp'; path.write_bytes(native)
    report=analyze(path,'limits'); units=report['limits']['work_units_used']; count=report['baked_lighting']['sampling']['observations']
    analyze(path,'exact-limits',options=['-max-work',units,'-lighting-max-samples',count])
    fails(path,'sample-limit',['-lighting','-lighting-max-samples',count-1],'observation limit exceeded')
    fails(path,'work-limit',['-lighting','-max-work',units-1],'work budget exceeded')
    fails(path,'missing-lighting',['-lighting-stride',2],'require -lighting')
    for option,value in (('-lighting-stride',0),('-lighting-stride',1025),('-lighting-max-samples',200001)):
        fails(path,f'bad-{option}-{value}',['-lighting',option,value],option)

    invalid=parts(native); vertices=bytearray(invalid[10]); struct.pack_into('<f',vertices,20,float('nan')); invalid[10]=vertices
    broken=root/'nonfinite.bsp'; broken.write_bytes(pack(native,invalid))
    fails(broken,'nonfinite',['-lighting'],'active lightmap coordinate')
    fails(broken,'nonfinite-force',['-lighting','-force'],'active lightmap coordinate')
    wide,_,_,_=synthetic(mode='wide'); large=parts(wide); large[13]*=2
    large[7]=struct.pack('<6f4i',0,0,0,64,64,128,0,8,0,0)
    path=root/'report-ceiling.bsp'; path.write_bytes(pack(wide,large))
    fails(path,'report-ceiling',['-lighting','-lighting-max-samples',200000],'report exceeds 64 MiB')

    for value,status in (('64 64 128','conventional_layout_count_matches'),('+64 64 128','conventional_layout_count_matches'),
                         ('64 64 64','layout_count_mismatch'),('0 64 128','invalid_stored_pitch'),
                         ('nan 64 128','invalid_stored_pitch'),('inf 64 128','invalid_stored_pitch'),
                         ('64 64','invalid_stored_pitch'),('64 64 128 junk','invalid_stored_pitch'),
                         ('+-64 64 128','invalid_stored_pitch'),('1e-310 64 128','layout_count_mismatch')):
        lump=parts(native); lump[0]=('{\n"classname" "worldspawn"\n"gridsize" "'+value+'"\n}\n\0').encode()
        path=root/f'grid-{len(summaries)}.bsp'; path.write_bytes(pack(native,lump))
        data=analyze(path,path.stem)['baked_lighting']; assert data['grid']['status']==status
        assert all((s['conventional_position'] is not None)==(status=='conventional_layout_count_matches') for s in data['grid']['observations'])

    # Actual compile -> VIS -> bake, including controls whose illumination cannot
    # be explained as missing point lights. Entity removal must not alter evidence.
    for mode in ('point','sun','emitter','mixed-deluxe','raven-point'):
        folder=root/mode; raven=mode=='raven-point'; game='ja' if raven else 'quake3'
        source=(create_lighting_fixture(folder) if mode in ('sun','emitter','mixed-deluxe') else
                create_fixture(folder,shader_directory='shaders' if raven else 'scripts'))
        text=source.read_text()
        if mode in ('sun','emitter'):
            text=re.sub(r'\{\n"classname" "light"\n[^{}]*\}\n','',text)
            if mode=='sun': text=text.replace('q3mapx/emitter','q3mapx/stone')
            else: text=text.replace('q3mapx/sky','q3mapx/stone')
        source.write_text(text)
        base=['-game',game,'-fs_basepath',folder,'-fs_homepath',root/'home','-fs_basegame','baseq3','-threads',4]
        run(exe,[*base,'-meta','-keeplights',source],folder,'bsp')
        run(exe,[*base,'-vis',source],folder,'vis')
        extra=['-deluxe','-bounce',1] if mode=='mixed-deluxe' else []
        run(exe,[*base,'-light','-fast','-samples',2,*extra,source],folder,'light',timeout=180)
        path=source.with_suffix('.bsp'); before=path.read_bytes(); lump=parts(before)
        assert lump[14] and max(lump[14])>0
        first=analyze(path,mode+'-retained',game,['-lighting-stride',4])['baked_lighting']
        assert first['sampling']['observations']>100
        assert any(slot['observations'] for surf in first['surfaces'] for slot in surf['lightmap_slots'])
        if mode in ('point','mixed-deluxe','raven-point'): assert b'"classname" "light"' in lump[0]
        lump[0]=re.sub(rb'\{\s*"classname" "light"[^{}]*\}\s*',b'',lump[0])
        assert b'"classname" "light"' not in lump[0]
        stripped=folder/'stripped.bsp'; stripped.write_bytes(pack(before,lump))
        second=analyze(stripped,mode+'-stripped',game,['-lighting-stride',4])['baked_lighting']
        assert first==second
        # No assets are used: reading the stored shader table is sufficient.
        if mode=='point':
            unsupported=root/'unsupported.bsp'; unsupported.write_bytes(native_file(Bsp(path),12))
            assert analyze(unsupported,'unsupported','fakk2')['baked_lighting']['status']=='unsupported_native_adapter'

    (root/'results.json').write_text(json.dumps({'schema_version':1,'cases':summaries,'output_preservation_cases':failures},indent=2)+'\n')
    print(f'{len(summaries)} lighting evidence reports and {len(failures)} preserved-output failures passed: exact coverage, encoded bytes/styles, grid indirection, budgets and five native bakes')


if __name__=='__main__': main()

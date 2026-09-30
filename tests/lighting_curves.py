"""Native curved/constant lightmap evidence with independent polynomial oracles.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from decimal import Decimal as D, localcontext
from fractions import Fraction as F
import hashlib
import itertools
import json
import math
from pathlib import Path
import struct
import subprocess

from bezier_uv import bernstein
from integration import run
from lighting_evidence import synthetic, parts, pack

POSITIONS=[{(1,0):F(16)},{(0,1):F(32)},
           {(1,0):F(64),(2,0):F(-64),(0,1):F(32),(0,2):F(-32),(1,1):F(16)}]


def restrict(poly,col,row,cols,rows):
    out={}
    for (i,j),coefficient in poly.items():
        for u in range(i+1):
            for v in range(j+1):
                key=(u,v)
                out[key]=out.get(key,F(0))+coefficient*math.comb(i,u)*math.comb(j,v)*F(col**(i-u),cols**i)*F(row**(j-v),rows**j)
    return out


def controls(polynomials,cols,rows):
    width,height=2*cols+1,2*rows+1
    net=[None]*(width*height)
    for row in range(rows):
        for col in range(cols):
            tile=list(zip(*(bernstein(restrict(p,col,row,cols,rows)) for p in polynomials)))
            for y in range(3):
                for x in range(3):
                    index=(row*2+y)*width+col*2+x
                    assert net[index] is None or net[index]==tile[y*3+x]
                    net[index]=tile[y*3+x]
    return net


def uv_polynomials(mode):
    x={(1,0):F(8),(0,0):F(1,2)}; y={(0,1):F(8),(0,0):F(1,2)}
    if mode=='rotated': x,y=y,{(1,0):F(-8),(0,0):F(17,2)}
    if mode=='mirrored': x={(1,0):F(-8),(0,0):F(17,2)}
    if mode=='sheared': x[(0,1)]=F(2)
    if mode=='curved': x={(1,0):F(12),(2,0):F(-4),(0,0):F(1,2)}
    if mode=='coupled':
        x.update({(1,1):F(4),(2,1):F(-4)}); y.update({(1,1):F(4),(1,2):F(-4)})
    if mode=='boundary_bow': y.update({(1,0):F(2),(2,0):F(-2)})
    if mode.startswith('folded'): x={(2,0):F(16),(1,0):F(-16),(0,0):F(9,2)}
    if mode=='folded_two': y={(0,2):F(16),(0,1):F(-16),(0,0):F(9,2)}
    if mode=='constant': x,y={(0,0):F(7,2)},{(0,0):F(11,2)}
    if mode=='constant_offcenter': x,y={(0,0):F(9,4)},{(0,0):F(9,4)}
    if mode=='constant_outside': x,y={(0,0):F(-1)},{(0,0):F(3,2)}
    if mode=='rank_one': y={(0,0):F(5,2)}
    return [x,y]


def make_patch(game,mode):
    native,_,_,_=synthetic(game,'patch'); lump=parts(native)
    raven=game!='quake3'; size=512 if game=='qfusion' else 128
    cols=rows=2 if mode=='multi_tile' else 1
    xyz=controls(POSITIONS,cols,rows); uv=controls(uv_polynomials(mode),cols,rows)
    stride=80 if raven else 44; surface_stride=148 if raven else 104
    verts=bytearray()
    for i,(position,texcoord) in enumerate(zip(xyz,uv)):
        vertex=bytearray(lump[10][:stride])
        coordinates=[2,3,4] if mode=='zero_geometry' else [float(p)+(1e7 if mode=='large_translation' else 0) for p in position]
        struct.pack_into('<3f',vertex,0,*coordinates)
        for slot in range(2 if raven else 1):
            coordinate=[F(7,2),F(11,2)] if slot and mode=='style_mix' else texcoord
            struct.pack_into('<2f',vertex,20+8*slot,*(float(v)/size for v in coordinate))
        normal=[0,0,0 if mode=='zero_normals' else 1]
        if mode=='varying_normals': normal=[int(axis==i%3) for axis in range(3)]
        struct.pack_into('<3f',vertex,52 if raven else 28,*normal)
        verts.extend(vertex)
    lump[10]=verts; lump[11]=b''
    surface=bytearray(lump[13][:surface_stride])
    struct.pack_into('<i',surface,16,len(xyz))
    struct.pack_into('<2i',surface,surface_stride-8,2*cols+1,2*rows+1)
    lump[13]=surface*4
    return pack(native,lump),cols,rows,size


def roots(mode,x,y):
    """Analytic inverses, or high-precision Newton on explicitly stated powers."""
    with localcontext() as context:
        context.prec=70
        a,b=D(x)/8,D(y)/8
        if mode=='rotated': u,v=1-b,a
        elif mode=='mirrored': u,v=1-a,b
        elif mode=='sheared': u,v=(D(x)-2*b)/8,b
        elif mode=='curved':
            if x>8: return []
            u,v=(D(3)-(D(9)-D(x)).sqrt())/2,b
        elif mode=='boundary_bow': u,v=a,(D(y)-2*a*(1-a))/8
        elif mode=='coupled':
            # Each component is monotone in its own parameter on the unit
            # square and reaches its maximum at that parameter's upper edge.
            if x>8 or y>8: return []
            u,v=a,b
            for _ in range(60):
                f=u+D('.5')*u*(1-u)*v-a; g=v+D('.5')*v*(1-v)*u-b
                fu=1+D('.5')*(1-2*u)*v; fv=D('.5')*u*(1-u)
                gu=D('.5')*v*(1-v); gv=1+D('.5')*(1-2*v)*u
                det=fu*gv-fv*gu
                u,v=u-(f*gv-g*fv)/det,v-(g*fu-f*gu)/det
            assert abs(u+D('.5')*u*(1-u)*v-a)<D('1e-55') and abs(v+D('.5')*v*(1-v)*u-b)<D('1e-55')
        else: u,v=a,b
        return [(float(u),float(v))] if 0<=u<=1 and 0<=v<=1 else []


def position(u,v): return [16*u,32*v,64*u*(1-u)+32*v*(1-v)+16*u*v]


def geometric_normal(u,v):
    vector=[-(64-128*u+16*v)/16,-(32-64*v+16*u)/32,1]
    length=math.sqrt(sum(x*x for x in vector)); return [x/length for x in vector]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler',type=Path,required=True); p.add_argument('--work-dir',type=Path,required=True)
    a=p.parse_args(); root=a.work_dir.resolve(); root.mkdir(parents=True,exist_ok=True); exe=a.compiler.resolve()
    common=['-fs_basepath',root,'-fs_homepath',root/'home','-fs_basegame','baseq3']
    summaries=[]; maximum_position=maximum_normal=maximum_stored_normal=0

    def analyze(native,label,game,stride=1,threads=4):
        source=root/f'{game}-{label}.bsp'; source.write_bytes(native); dest=source.with_suffix('.json')
        run(exe,['-game',game,*common,'-threads',threads,'-bsp-evidence','-lighting','-lighting-stride',stride,'-report',dest,source],root,game+'-'+label,timeout=120)
        report=json.loads(dest.read_text()); assert source.read_bytes()==native
        assert report['source']['sha256']==hashlib.sha256(native).hexdigest()
        data=report['baked_lighting']
        count=len(data['grid']['observations'])+sum(len(s['vertex_observations'])+sum(len(z['observations'])+len(z['patch_observations'])+len(z['constant_regions']) for z in s['lightmap_slots']) for s in data['surfaces'])
        assert count==data['sampling']['observations']
        summaries.append({'case':game+'-'+label,'observations':count,'work':report['limits']['work_units_used']})
        return report

    modes=('linear','rotated','mirrored','sheared','curved','coupled','boundary_bow','folded_one','folded_two',
           'constant','constant_offcenter','constant_outside','rank_one','multi_tile','zero_geometry','zero_normals','varying_normals','style_mix','large_translation')
    for game,mode in itertools.product(('quake3','ja','qfusion'),modes):
        native,cols,rows,size=make_patch(game,mode)
        for stride in ((1,3) if mode in ('linear','folded_one','constant','multi_tile') else (1,)):
            label=f'{mode}-{stride}'
            report=analyze(native,label,game,stride); data=report['baked_lighting']
            second=analyze(native,label+'-single',game,stride,1)
            # These invocations intentionally use distinct preserved BSP copies.
            assert report['limits']==second['limits'] and data==second['baked_lighting']
            for surface in data['surfaces']:
                assert surface['vertex_role']=='bezier_control'
                for slot in surface['lightmap_slots'][:2 if game!='quake3' else 1]:
                    index=slot['slot']; constant=mode.startswith('constant') or mode=='style_mix' and index==1
                    assert slot['status']=='bezier_analyzed' and not slot['observations']
                    if constant:
                        assert slot['constant_primitive_regions']==cols*rows
                        assert len(slot['constant_regions'])==(cols*rows+stride-1)//stride and not slot['patch_observations']
                        for record in slot['constant_regions']:
                            assert record['primitive_kind']=='bezier_tile' and record['primitive']%stride==0
                            u=(record['primitive']%cols+.5)/cols; v=(record['primitive']//cols+.5)/rows
                            assert max(abs(a-b) for a,b in zip(record['representative_position'],position(u,v)))<1e-10
                            footprint=record['footprint']
                            if mode=='constant_outside': assert not footprint and record['footprint_status']=='outside_internal_texel_centers'
                            else:
                                assert sum(f['weight'] for f in footprint)==1
                                assert len(footprint)==(4 if mode=='constant_offcenter' else 1)
                                expected_footprint={(1,1):1/16,(2,1):3/16,(1,2):3/16,(2,2):9/16} if mode=='constant_offcenter' else {(3,5):1}
                                assert {tuple(f['texel']):f['weight'] for f in footprint}==expected_footprint
                                for f in footprint:
                                    x,y=f['texel']; assert f['rgb']==([255,42,0] if index else [x,y,(x+3*y)%256])
                        continue
                    assert not slot['constant_regions'] and slot['patch_observations'],(game,mode)
                    actual={tuple(s['texel']):s for s in slot['patch_observations']}
                    if mode.startswith('folded'):
                        assert set(actual)==set(itertools.product(range(0,5,stride),range(0,5 if mode=='folded_two' else 9,stride)))
                    if mode=='rank_one': assert set(actual)=={(x,2) for x in range(9)}
                    for (x,y),sample in actual.items():
                        assert x%stride==0 and y%stride==0
                        assert sample['rgb']==([255,42,0] if index else [x,y,(x+3*y)%256])
                        if mode.startswith('folded'):
                            singular=x==0 or mode=='folded_two' and y==0
                            assert sample['position'] is None
                            assert sample['unresolved_coverage'] if singular else sample['ambiguous_mapping']
                            continue
                        if mode=='rank_one':
                            assert y==2 and sample['unresolved_coverage'] and sample['position'] is None
                            continue
                        expected=roots(mode,x,y)
                        if not expected:
                            assert sample['position'] is None and sample['unresolved_coverage']; continue
                        assert sample['position'] is not None and not sample['unresolved_coverage'] and not sample['ambiguous_mapping'],(game,mode,sample)
                        u,v=expected[0]; wanted=[2,3,4] if mode=='zero_geometry' else position(u,v)
                        if mode=='large_translation': wanted=[p+1e7 for p in wanted]
                        error=max(abs(a-b) for a,b in zip(sample['position'],wanted)); maximum_position=max(maximum_position,error)
                        assert error<1e-6,(game,mode,sample,wanted)
                        if mode=='varying_normals':
                            field=[(1-u)**2,2*u*(1-u),u*u]; length=math.sqrt(sum(n*n for n in field))
                            error=max(abs(a-b/length) for a,b in zip(sample['normal'],field))
                            maximum_stored_normal=max(maximum_stored_normal,error); assert error<1e-7
                        else: assert sample['normal']==(None if mode=='zero_normals' else [0,0,1])
                        if mode=='zero_geometry': assert sample['geometric_normal'] is None
                        else:
                            error=max(abs(a-b) for a,b in zip(sample['geometric_normal'],geometric_normal(u,v)))
                            maximum_normal=max(maximum_normal,error); assert error<1e-7
                    if not mode.startswith('folded') and mode!='rank_one':
                        for x,y in itertools.product(range(0,12,stride),repeat=2):
                            if roots(mode,x,y): assert (x,y) in actual,(game,mode,'missing texel',x,y)

    # New geometry sampling shares the same transactional limits as the legacy
    # atlas/vertex/grid observations, including failure inside curve worker jobs.
    native,_,_,_=make_patch('quake3','coupled'); reference=analyze(native,'limits','quake3')
    source=root/'quake3-limits.bsp'; output=root/'preserved.json'; failures=[]
    for label,option,limit,needle in (
        ('work','-max-work',reference['limits']['work_units_used']-1,'work budget exceeded'),
        ('samples','-lighting-max-samples',reference['baked_lighting']['sampling']['observations']-1,'observation limit exceeded')):
        output.write_text('preserved\n')
        command=[exe,'-game','quake3',*common,'-threads',4,'-bsp-evidence','-lighting',option,limit,'-report',output,source]
        result=subprocess.run(list(map(str,command)),cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=120)
        log=result.stdout.decode(errors='replace'); (root/f'limit-{label}.log').write_text(log)
        assert result.returncode==1 and needle in log and output.read_text()=='preserved\n' and source.read_bytes()==native
        assert not list(root.glob('*.q3mapx-*')); failures.append(label)
    summary={'cases':summaries,'preserved_outputs':failures,'max_position_error':maximum_position,
             'max_geometric_normal_error':maximum_normal,'max_stored_normal_error':maximum_stored_normal}
    (root/'results.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(f'{len(summaries)} native curved/constant reports and {len(failures)} limit failures passed; max XYZ/normal error {maximum_position:.3g}/{maximum_normal:.3g}')


if __name__=='__main__': main()

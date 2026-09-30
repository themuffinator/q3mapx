"""Independent rational polynomial oracle for bounded biquadratic UV inversion.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from fractions import Fraction as F
import itertools
import json
from pathlib import Path
import subprocess


def bernstein(polynomial):
    # Power -> degree-two Bernstein in each axis, independently of the C++
    # evaluator/subdivision. All generated controls and targets are dyadic.
    basis=((F(1),F(0),F(0)),(F(1),F(1,2),F(0)),(F(1),F(1),F(1)))
    return [sum(value*basis[x][i]*basis[y][j] for (i,j),value in polynomial.items())
            for y in range(3) for x in range(3)]


def value(polynomial,u,v):
    return sum(coefficient*u**i*v**j for (i,j),coefficient in polynomial.items())


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--unit',type=Path,required=True); p.add_argument('--work-dir',type=Path,required=True)
    a=p.parse_args(); root=a.work_dir.resolve(); root.mkdir(parents=True,exist_ok=True)
    cases=[]
    transforms=[((1,0),(0,1)),((0,-8),(4,0)),((4,2),(1,4)),((-8,1),(2,4))]
    for folded in (0,1,2):
        x={(1,0):F(1)} if folded==0 else {(2,0):F(4),(1,0):F(-4),(0,0):F(1)}
        y={(0,1):F(1)} if folded<2 else {(0,2):F(4),(0,1):F(-4),(0,0):F(1)}
        for transform in transforms:
            powers=[]
            for axis,row in enumerate(transform):
                powers.append({key:row[0]*x.get(key,0)+row[1]*y.get(key,0)+(F(7+axis) if key==(0,0) else 0)
                               for key in set(x)|set(y)|{(0,0)}})
            net=list(zip(*(bernstein(poly) for poly in powers)))
            for u,v in itertools.product((F(0),F(1,8),F(3,8),F(1,2),F(7,8),F(1)),repeat=2):
                roots=set(itertools.product((u,1-u) if folded else (u,),(v,1-v) if folded==2 else (v,)))
                singular=folded and u==F(1,2) or folded==2 and v==F(1,2)
                cases.append({'kind':f'fold-{folded}','net':net,'target':[value(poly,u,v) for poly in powers],
                              'roots':roots,'singular':bool(singular)})
    for k in (F(1,8),F(1,2)):
        powers=[{(1,0):F(1),(1,1):k,(2,1):-k},{(0,1):F(1),(1,1):k,(1,2):-k}]
        net=list(zip(*(bernstein(poly) for poly in powers)))
        for u,v in itertools.product((F(0),F(1,8),F(1,4),F(1,2),F(3,4),F(1)),repeat=2):
            cases.append({'kind':'coupled','net':net,'target':[value(poly,u,v) for poly in powers],
                          'roots':{(u,v)},'singular':False})
    for x,y in ((-1,0),(0,-1),(2,0),(0,2),(2,2)):
        cases.append({'kind':'outside','net':list(zip(bernstein({(1,0):F(1)}),bernstein({(0,1):F(1)}))),
                      'target':[F(x),F(y)],'roots':set(),'singular':False})
    cases.append({'kind':'constant','net':[(F(2),F(3))]*9,'target':[F(2),F(3)],'roots':set(),'singular':True})
    cases.append({'kind':'rank-one','net':[(F(i%3),F(0)) for i in range(9)],'target':[F(1),F(0)],'roots':set(),'singular':True})
    cases.append({'kind':'rotated-rank-one','net':[(F(i%3),F(i%3)) for i in range(9)],'target':[F(1),F(1)],'roots':set(),'singular':True})
    data=str(len(cases))+'\n'+'\n'.join(' '.join(format(float(v),'.17g') for point in c['net'] for v in point)+' '+
                                      ' '.join(format(float(v),'.17g') for v in c['target']) for c in cases)+'\n'
    (root/'queries.txt').write_text(data)
    process=subprocess.run([str(a.unit.resolve())],input=data,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,cwd=root,timeout=120)
    (root/'answers.txt').write_text(process.stdout); (root/'unit.log').write_text(process.stderr)
    assert process.returncode==0,process.stderr
    lines=process.stdout.splitlines(); assert len(lines)==len(cases)
    maximum=0; unresolved=0; boundary=0; max_nodes=0
    for case,line in zip(cases,lines):
        tokens=line.split(); nodes,regions,work,unknown,hits=map(int,tokens[:5]); records=tokens[5:]
        assert nodes<=1023 and hits<=64 and len(records)==6*hits and work<=1_000_000
        max_nodes=max(max_nodes,nodes); unresolved+=bool(unknown)
        if case['kind']=='rotated-rank-one': assert nodes==1023 and regions>0
        actual=[]
        for i in range(hits):
            u,v,ru,rv,error=map(float,records[i*6:i*6+5]); edge=int(records[i*6+5]); boundary+=bool(edge)
            assert 0<=u<=1 and 0<=v<=1 and 0<=ru<=2e-9 and 0<=rv<=2e-9 and 0<=error<=1e-7
            actual.append((u,v))
            if case['roots']:
                distance=min(max(abs(u-float(x)),abs(v-float(y))) for x,y in case['roots'])
                maximum=max(maximum,distance); assert distance<=2e-8,(case,line,'false root')
                assert any(abs(u-float(x))<=ru+2e-15 and abs(v-float(y))<=rv+2e-15 for x,y in case['roots']),(case,line,'radius did not enclose known root')
        if case['singular']:
            assert unknown,(case,line,'singular coverage claimed resolved')
        else:
            assert not unknown,(case,line,'regular query unresolved')
            for expected in case['roots']:
                assert any(max(abs(u-float(expected[0])),abs(v-float(expected[1])))<=2e-8 for u,v in actual),(case,line,'missing root')
            if not case['roots']: assert not actual
    summary={'cases':len(cases),'max_parameter_error':maximum,'unresolved_singular_queries':unresolved,
             'boundary_hits':boundary,'max_nodes':max_nodes}
    (root/'results.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary))


if __name__=='__main__': main()

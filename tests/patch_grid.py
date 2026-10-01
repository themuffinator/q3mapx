"""Rational polynomial oracle for exact patch control-field reduction.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from fractions import Fraction as F
import json
import math
from pathlib import Path
import random
import struct
import subprocess


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def number(value):
    return struct.unpack('<f', struct.pack('<I', value))[0]


def oracle(kind, values):
    if kind == 'float':
        values = [number(v) for v in values]
        if not all(map(math.isfinite, values)): return None
    a,b,c,d,e = map(F, values)
    # Expand each half into a polynomial over the complete [0,1] domain.
    left = (a, 4*(b-a), 4*(a-2*b+c))
    right = (4*c-4*d+e, 12*d-8*c-4*e, 4*(c-2*d+e))
    if left != right: return None
    candidate = a + left[1]/2
    if kind == 'byte':
        return int(candidate) if 0 <= candidate <= 255 and candidate.denominator == 1 else None
    try: stored = bits(float(candidate))
    except (OverflowError, ValueError): return None
    if not math.isfinite(number(stored)) or F(number(stored)) != candidate: return None
    return stored


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--unit',required=True,type=Path)
    parser.add_argument('--work-dir',required=True,type=Path)
    args=parser.parse_args()
    root=args.work_dir.resolve(); root.mkdir(parents=True,exist_ok=True)
    rng=random.Random(0xB32C0104)
    cases=[]
    def add(kind, values): cases.append((kind,list(values)))
    def subdivide(a,b,c): return (a,(a+b)/2,(a+2*b+c)/4,(b+c)/2,c)
    for _ in range(5000):
        a,b,c=(rng.randint(-8192,8192) for _ in range(3))
        scale=math.ldexp(1.0,rng.randint(-130,110))
        split=list(map(bits,subdivide(a*scale,b*scale,c*scale)))
        add('float',split)
        i=rng.randrange(5); split[i] ^= 1; add('float',split)
        add('float',[rng.getrandbits(32) for _ in range(5)])
    for _ in range(5000):
        source=[4*rng.randrange(64) for _ in range(3)]
        split=[int(x) for x in subdivide(*source)]
        add('byte',split)
        i=rng.randrange(5); split[i] ^= 1; add('byte',split)
        add('byte',[rng.randrange(256) for _ in range(5)])
    # Large exponent gaps where rounded binary64 equations could hide residuals,
    # subnormals, signed zeros, non-finite channels and unrepresentable middles.
    for tiny in (1,0x80000001,0x00800000):
        for large in (0x71800000,0xF1800000,0x3F800000):
            add('float',[tiny,large,large,large,tiny])
            add('float',[large,tiny,0,tiny,large])
    for special in (0,0x80000000,1,0x007FFFFF,0x00800000,0x7F7FFFFF,0x7F800000,0xFF800000,0x7FC00001):
        add('float',[special]*5)
    add('byte',[0,200,200,200,0])  # one quadratic, but middle exceeds byte storage
    add('float',[bits(v) for v in (0,3e38,3e38,3e38,0)])
    request=''.join(kind+' '+' '.join(map(str,values))+'\n' for kind,values in cases)
    result=subprocess.run([str(args.unit.resolve())],input=request,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=True,timeout=90)
    (root/'unit.stderr.log').write_text(result.stderr,encoding='utf-8')
    lines=result.stdout.splitlines()
    assert len(lines)==len(cases)
    accepted={'float':0,'byte':0}
    for (kind,values),line in zip(cases,lines):
        good,value=map(int,line.split())
        expected=oracle(kind,values)
        assert bool(good)==(expected is not None),(kind,values,line,expected)
        if good:
            if kind=='float' and number(value)==0: assert number(expected)==0
            else: assert value==expected,(kind,values,value,expected)
            accepted[kind]+=1
    record={'cases':len(cases),'accepted':accepted,'oracle':'exact rational polynomial equality and storage representability',
            'nondefault_rounding_rejected':True,'failure_output_preserved':True}
    (root/'results.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print(f"Patch grid: {len(cases)} rational oracle cases; {accepted} accepted")


if __name__=='__main__': main()

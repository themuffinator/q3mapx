"""Independent rational weighted-regression oracle and permutation controls.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from fractions import Fraction as F
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess


def oracle(samples):
    # Rational Gaussian elimination on the full 3-column design. Independent of
    # the production centered 2x2 covariance implementation and all its tolerances.
    a = [[F(0) for _ in range(5)] for _ in range(3)]
    for x, y, u, v, w in samples:
        p = [F(x), F(y), F(1)]
        for i in range(3):
            for j in range(3): a[i][j] += F(w)*p[i]*p[j]
            a[i][3] += F(w)*p[i]*F(u)
            a[i][4] += F(w)*p[i]*F(v)
    for i in range(3):
        pivot = next(j for j in range(i, 3) if a[j][i])
        a[i], a[pivot] = a[pivot], a[i]
        scale = a[i][i]
        a[i] = [value/scale for value in a[i]]
        for j in range(3):
            if j != i:
                scale = a[j][i]
                a[j] = [x-scale*y for x, y in zip(a[j], a[i])]
    return [float(a[i][3+axis]) for axis in range(2) for i in range(3)]


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--unit', type=Path, required=True)
    p.add_argument('--work-dir', type=Path, required=True)
    a = p.parse_args()
    root = a.work_dir.resolve(); root.mkdir(parents=True, exist_ok=True)
    rng = random.Random(302609)
    cases, labels, expected = [], [], []
    for index in range(80):
        offset = [0, 1024, -1e6, 1e7][index % 4]
        rows = [[rng.randint(1, 24)/32, rng.randint(-24, -1)/32, rng.randint(-8, 8)] for _ in range(2)]
        samples = []
        for y in range(4):
            for x in range(4):
                xy = [x*8+rng.randint(-4, 4)/8, y*8+rng.randint(-4, 4)/8]
                uv = [f32(row[0]*xy[0]+row[1]*xy[1]+row[2]+rng.randint(-4,4)/2**25) for row in rows]
                samples.append([xy[0]+offset, xy[1]-offset, *uv, rng.randint(1, 32)/8])
        result = oracle(samples)
        for mode in ('original', 'shuffled', 'scaled-weights'):
            modified = [s[:] for s in samples]
            if mode == 'shuffled': rng.shuffle(modified)
            if mode == 'scaled-weights':
                for s in modified: s[-1] *= 1024
            cases.append(modified); labels.append(f'{index}-{mode}'); expected.append(result)
    lines = [str(len(cases))]
    for samples in cases:
        lines.append(str(len(samples)))
        lines.extend(' '.join(format(v, '.17g') for v in sample) for sample in samples)
    encoded = ('\n'.join(lines)+'\n').encode()
    result = subprocess.run([str(a.unit.resolve()), '--oracle'], input=encoded, capture_output=True, cwd=root, timeout=60)
    (root/'oracle.log').write_bytes(result.stdout+result.stderr)
    assert result.returncode == 0, result.stderr
    output = result.stdout.decode().splitlines()
    assert len(output) == len(cases)
    maximum = 0
    for index, (line, wanted) in enumerate(zip(output, expected)):
        fields = line.split()
        assert fields[0] == 'consistent', (labels[index], line)
        values = list(map(float, fields[1:7]))
        for axis, (actual, exact) in enumerate(zip(values, wanted)):
            error = abs(actual-exact); maximum = max(maximum, error)
            assert error <= (2e-7 if axis % 3 == 2 else 1e-12), (labels[index], axis, actual, exact)
        if index % 3: assert line == output[index-index%3], (labels[index], 'order or weight-scale dependence')
    report = {'cases': len(cases), 'oracle': 'exact rational normal equations with Gaussian elimination',
              'maximum_coefficient_error': maximum, 'permutation_and_weight_scale_identity': True,
              'input_sha256': hashlib.sha256(encoded).hexdigest(),
              'unit_sha256': hashlib.sha256(a.unit.read_bytes()).hexdigest()}
    (root/'oracle.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'{len(cases)} rational-oracle, permutation and weight-scaling cases passed')


if __name__ == '__main__': main()

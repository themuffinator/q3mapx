"""Exact rational oracles for predicates, mesh coverage and all interpolants.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from collections import Counter
from fractions import Fraction as F
import hashlib
import itertools
import json
import math
from pathlib import Path
import random
import struct
import subprocess


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def from_bits(value):
    return struct.unpack('<f', struct.pack('<I', value))[0]


def next_float(value):
    return from_bits(1 if value == 0 else bits(value)+(1 if value > 0 else -1))


def determinant_sign(points):
    # Rational Gaussian elimination is independent of the C++ product-expansion
    # implementation and never relies on a rounded geometric epsilon.
    matrix = [[F(v) for v in p]+[F(1)] for p in points]
    determinant = F(1)
    for column in range(len(matrix)):
        row = next((r for r in range(column, len(matrix)) if matrix[r][column]), None)
        if row is None:
            return 0
        if row != column:
            matrix[row], matrix[column] = matrix[column], matrix[row]
            determinant = -determinant
        pivot = matrix[column][column]
        determinant *= pivot
        for r in range(column+1, len(matrix)):
            weight = matrix[r][column]/pivot
            for c in range(column+1, len(matrix)):
                matrix[r][c] -= weight*matrix[column][c]
    return (determinant > 0)-(determinant < 0)


def mesh_boundary(triangles):
    result = Counter()
    for t in triangles:
        for a, b in zip(t, t[1:]+t[:1]):
            result[min(a, b), max(a, b)] += 1 if a < b else -1
    return {k: v for k, v in result.items() if v}


def cross(a, b, c):
    return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])


def sample(vertices, triangles, point):
    hits, strict = [], 0
    for tri in triangles:
        a, b, c = (vertices[i] for i in tri)
        area = cross(a, b, c)
        weights = cross(point, b, c)/area, cross(a, point, c)/area, cross(a, b, point)/area
        if min(weights) < 0:
            continue
        strict += min(weights) > 0
        hits.append(tuple(sum(w*p[f] for w, p in zip(weights, (a, b, c))) for f in range(32)))
    assert strict <= 1, 'Overlapping positive-area faces'
    assert hits and all(h == hits[0] for h in hits), 'Uncovered point or inconsistent shared edge'
    return hits[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--unit', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.unit.resolve(), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    rng = random.Random(20261002)

    def execute(option, payload):
        result = subprocess.run([str(exe), option], input=payload.encode(), stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, cwd=root, timeout=90)
        (root/(option[2:]+'.log')).write_bytes(result.stdout+result.stderr)
        assert result.returncode == 0, result.stderr.decode(errors='replace')
        return result.stdout.decode().splitlines()

    def random_float():
        while True:
            value = from_bits(rng.getrandbits(32))
            if math.isfinite(value):
                return value

    cases = []
    for dimensions in (2, 3):
        for trial in range(800):
            points = [tuple(random_float() for _ in range(dimensions)) for _ in range(dimensions+1)]
            cases.append(points)
            # Coincident rows and swaps test exact zero, not approximate zero.
            if trial % 8 == 0:
                repeated = points.copy(); repeated[-1] = repeated[0]; cases.append(repeated)
        for trial in range(400):
            exponent = rng.randint(-140, 110)
            scale = math.ldexp(1, exponent)
            a, b, c = (rng.randint(-4, 4) for _ in range(3))
            if dimensions == 2:
                points = [(f32(x*scale), f32((a*x+c)*scale))
                          for x in rng.sample(range(-128, 129), 3)]
            else:
                points = [(f32(x*scale), f32(y*scale), f32((a*x+b*y+c)*scale))
                          for x, y in [(rng.randint(-128, 128), rng.randint(-128, 128)) for _ in range(4)]]
            cases.append(points)
            perturbed = [list(p) for p in points]
            perturbed[-1][-1] = next_float(perturbed[-1][-1]); cases.append(perturbed)
        for _ in range(24):
            points = [tuple(random_float() for _ in range(dimensions)) for _ in range(dimensions+1)]
            cases.extend(itertools.permutations(points))
    # Cancellation across the full binary32 exponent range, signed zero,
    # subnormals and nearly coincident huge values are explicit controls.
    tiny, huge = from_bits(1), from_bits(0x7f7fffff)
    for x in (tiny, -tiny, 0.0, -0.0, 1.0, -1.0, huge, -huge):
        changed = next_float(x)
        if not math.isfinite(changed):
            changed = from_bits(bits(x)-1)
        cases.extend([[(huge, x), (-huge, x), (tiny, changed)],
                      [(huge, x, tiny), (-huge, x, -tiny), (tiny, huge, x), (0, -huge, changed)]])
    expected = [determinant_sign(p) for p in cases]
    payload = '\n'.join(str(len(p)-1)+' '+' '.join(format(bits(v), 'x') for row in p for v in row) for p in cases)+'\n'
    observed = list(map(int, execute('--predicates', payload)))
    assert len(observed) == len(expected)
    assert observed == expected, next((i, cases[i], a, b) for i, (a, b) in enumerate(zip(observed, expected)) if a != b)
    predicate_record = {'cases': len(cases), 'signs': dict(Counter(expected)),
                        'input_sha256': hashlib.sha256(payload.encode()).hexdigest(),
                        'oracle_sha256': hashlib.sha256(json.dumps(expected).encode()).hexdigest()}

    meshes, records = [], []
    for trial in range(32):
        n = 3+trial % 4
        scale = math.ldexp(1, [-140, -100, -20, 0, 30, 90, 110, 1][trial % 8])
        vertices, triangles = [], []
        for y in range(n+1):
            for x in range(n+1):
                values = [(x+2*y+1024)*scale, (3*x-y-4096)*scale, (2*x-3*y+512)*scale]
                values += [((f % 5)*x-(f % 7)*y+f)*math.ldexp(1, (f % 11)-5) for f in range(3, 32)]
                vertices.append(list(map(f32, values)))
        hole = trial % 3 == 0
        for y in range(n):
            for x in range(n):
                if hole and x == n//2 and y == n//2:
                    continue
                a = y*(n+1)+x; b, c, d = a+1, a+n+2, a+n+1
                triangles += [(a, b, d), (b, c, d)] if rng.randrange(2) else [(a, b, c), (a, c, d)]
        if trial % 2:
            triangles = [(b, a, c) for a, b, c in triangles]
        discontinuity = trial % 4 == 2
        if discontinuity:
            at = (n//2)*(n+1)+n//2
            vertices[at][31] = next_float(vertices[at][31])
        meshes.append((vertices, triangles, hole, discontinuity))
    payload = ''
    for vertices, triangles, _, _ in meshes:
        payload += f'{len(vertices)} {len(triangles)} 20000000\n'
        payload += '\n'.join(' '.join(format(bits(v), 'x') for v in p) for p in vertices)+'\n'
        payload += '\n'.join(' '.join(map(str, t)) for t in triangles)+'\n'
    lines = iter(execute('--mesh', payload))
    samples = 0
    for vertices, original, hole, discontinuity in meshes:
        count, edits, work, topology, planar, attributes = map(int, next(lines).split())
        triangles = [tuple(map(int, next(lines).split())) for _ in range(count)]
        assert len(original) == count+2*edits
        assert mesh_boundary(original) == mesh_boundary(triangles)
        exact_vertices = [[F(v) for v in p] for p in vertices]
        area = lambda t: cross(*(exact_vertices[i] for i in t))
        initial_area = sum(map(area, original))
        assert sum(map(area, triangles)) == initial_area and all(area(t)*initial_area > 0 for t in triangles)
        if not discontinuity:
            boundary_vertices = {v for e in mesh_boundary(original) for v in e}
            assert count == len(boundary_vertices)+(2 if hole else 0)-2
        else:
            assert attributes > 0
        # Sample both domains so the oracle checks missing and introduced area.
        for domain in (original, triangles):
            for tri in domain[::max(1, len(domain)//8)]:
                point = tuple(sum(F(weight, 7)*exact_vertices[i][f] for weight, i in zip((1, 2, 4), tri)) for f in range(2))
                assert sample(exact_vertices, original, point) == sample(exact_vertices, triangles, point)
                samples += 1
        records.append({'vertices': len(vertices), 'triangles_before': len(original), 'triangles_after': count,
                        'interior_vertices_removed': edits, 'work_units': work, 'hole': hole,
                        'one_ulp_field_discontinuity': discontinuity, 'topology_rejections': topology,
                        'planarity_rejections': planar, 'attribute_rejections': attributes})
    assert next(lines, None) is None
    (root/'validation.json').write_text(json.dumps({'schema_version': 1, 'result': 'passed',
        'unit_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'predicates': predicate_record,
        'meshes': records, 'exact_rational_coverage_interpolation_samples': samples,
        'mesh_input_sha256': hashlib.sha256(payload.encode()).hexdigest(),
        'scope': 'Mathematical mesh core only; no runtime material, renderer, BSP publication or visual-equivalence certification'}, indent=2)+'\n', encoding='utf-8')
    print(f'{len(cases)} rational determinant comparisons, {len(meshes)} exact mesh oracles and {samples} rational interpolation samples passed')


if __name__ == '__main__':
    main()

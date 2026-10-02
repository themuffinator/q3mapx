"""Independent controls for rational VIS interior-occlusion certificates.
SPDX-License-Identifier: GPL-3.0-or-later.
"""
from fractions import Fraction as F
from itertools import permutations, product
import copy
import random
from vis_occlusion import Budget, area2, box, certify_occlusion, clip, hull, reference_clip, replay_certificate


def segment_intersects(first, second, cell):
    """Independent continuous closed-segment/slab check for clear-ray controls."""
    begin, end = F(0), F(1)
    for a, b, lo, hi in zip(first, second, *cell):
        if a == b:
            if not lo <= a <= hi:
                return False
        else:
            enter, leave = sorted(((lo-a)/(b-a), (hi-a)/(b-a)))
            begin, end = max(begin, enter), min(end, leave)
            if begin > end:
                return False
    return True


def main():
    rng = random.Random(20261002)
    clipping_cases = 0
    for _ in range(160):
        poly = hull([(F(rng.randrange(-20, 21)), F(rng.randrange(-20, 21))) for _ in range(12)])
        for _ in range(8):
            half = (rng.randrange(-9, 10), rng.randrange(-9, 10), rng.randrange(-30, 31))
            if not any(half[:2]):
                continue
            actual = clip(poly, half, Budget())
            expected = reference_clip(poly, half, Budget())
            assert hull(actual) == hull(expected) and abs(area2(actual)) == abs(area2(expected))
            clipping_cases += 1
            poly = actual

    a = ((-2, -1, -1), (0, 1, 1))
    b = ((10, -1, -1), (12, 1, 1))
    wall = ((4, -4, -4), (6, 4, 4))
    half = [((4, -4, -4), (6, 0, 4)), ((4, 0, -4), (6, 4, 4))]
    checked = 0
    for order in permutations(range(3)):
        for signs in product((-1, 1), repeat=3):
            def transform(value):
                points = [tuple(signs[i]*F(p[order[i]])+2**40+i for i in range(3)) for p in value]
                return tuple(tuple(op(p[i] for p in points) for i in range(3)) for op in (min, max))
            for obstacles in ([wall], half):
                left, right, blocks = transform(a), transform(b), list(map(transform, obstacles))
                result = certify_occlusion(left, right, blocks)
                assert result['status'] == 'occluded_interiors', result
                assert replay_certificate(left, right, blocks, result['certificate'])
                assert certify_occlusion(right, left, blocks)['status'] == 'occluded_interiors'
                assert not replay_certificate(left, right, blocks, dict(axis=0, cuts=[]))
                checked += 1

    # An arbitrarily small *positive* window must survive; no fixed epsilon may
    # turn it into a certificate. y=z=0 gives an independent explicit clear ray.
    for exponent in (4, 24, 80):
        gap = F(1, 2**exponent)
        open_window = [((4, -4, -4), (6, -gap, 4)), ((4, gap, -4), (6, 4, 4))]
        result = certify_occlusion(a, b, open_window, depth=0)
        assert result['status'] == 'unresolved', result
        assert not replay_certificate(a, b, open_window, dict(axis=0, cuts=[[0, 1], [1, 1]]))
        closed_window = [((4, -4, -4), (6, gap, 4)), ((4, -gap, -4), (6, 4, 4))]
        result = certify_occlusion(a, b, closed_window)
        assert result['status'] == 'occluded_interiors'
        assert replay_certificate(a, b, closed_window, result['certificate'])
        checked += 2
    for obstacles in ([], [((4, -4, -4), (6, 4, F(9, 10)))], [((4, 2, -4), (6, 4, 4))]):
        assert certify_occlusion(a, b, obstacles, depth=0)['status'] == 'unresolved'
        checked += 1
    touching = [((0, -4, -4), (1, 4, 4))]
    result = certify_occlusion(a, b, touching)
    assert result['status'] == 'occluded_interiors' and replay_certificate(a, b, touching, result['certificate'])
    assert certify_occlusion(a, b, [wall], work=1)['status'] == 'work_limit'
    assert certify_occlusion(a, b, [((4, -F(1, 2), -4), (6, F(1, 2), 4))], states=1)['status'] == 'state_limit'
    assert not replay_certificate(a, b, [wall], dict(axis=0, cuts=[[-1, 0]]))
    assert not replay_certificate(a, b, [wall], dict(split_axis=0, split_cell=0, children=[]))
    for invalid in ((((0, 0, 0), (0, 1, 1))), (((0, 0, 0), (1, float('inf'), 1))), ((0, 0), (1, 1))):
        try:
            box(invalid)
        except (ValueError, OverflowError):
            pass
        else:
            raise AssertionError('Invalid oracle box accepted')
    # Reduced native regression: independent 2D projections admit impossible
    # endpoint combinations and lines hitting a blocker between its three cuts.
    a = box(((236, -276, 80), (276, -236, 176)))
    b = box(((-492, -532, 0), (-404, -492, 80)))
    blocks = list(map(box, [((-404, -532, 0), (-364, -492, 80)),
                           ((-276, -404, 0), (-236, -364, 112)),
                           ((-148, -404, 0), (-108, -364, 128)),
                           ((-20, -404, 0), (20, -364, 144))]))
    assert certify_occlusion(a, b, blocks, refinement=0)['status'] == 'unresolved'
    refined_cases = 0
    for order in permutations(range(3)):
        for signs in product((-1, 1), repeat=3):
            def transform(value):
                points = [tuple(signs[i]*p[order[i]]+2**40+i for i in range(3)) for p in value]
                return tuple(tuple(op(p[i] for p in points) for i in range(3)) for op in (min, max))
            for first, second in ((a, b), (b, a)):
                left, right, solids = transform(first), transform(second), list(map(transform, blocks))
                result = certify_occlusion(left, right, solids)
                assert result['status'] == 'occluded_interiors', (order, signs, result)
                assert replay_certificate(left, right, solids, result['certificate'])
                refined_cases += 1
    result = certify_occlusion(a, b, blocks)
    proof = result['certificate']
    assert 'refinements' in proof, 'Regression must exercise the new certificate path'
    limited = certify_occlusion(a, b, blocks, work=result['work']-1)
    assert limited['status'] == 'work_limit' and limited['certificate'] is None
    for invalid in (-1, 17):
        try:
            certify_occlusion(a, b, blocks, refinement=invalid)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid refinement limit accepted')
    malformed = copy.deepcopy(proof)
    malformed['refinements'].pop()
    assert not replay_certificate(a, b, blocks, malformed)
    for replacement in (dict(inside_occluder=-1), dict(inside_occluder=0),
                        dict(outside_endpoint=0, direction=1), dict(split_side=2, anchor=0, children=[])):
        malformed = copy.deepcopy(proof)
        malformed['refinements'][0] = replacement
        assert not replay_certificate(a, b, blocks, malformed), replacement
    assert not replay_certificate(a, b, [], proof)

    # Deliberately retain a known rational clear sightline, then populate the
    # region with boxes that miss its complete segment. Refined predicates may
    # not reject it, irrespective of their branch choices or floating epsilons.
    clear_cases = 0
    for _ in range(80):
        p = tuple(F(rng.randrange(-12, -4)) for _ in range(3))
        q = tuple(F(rng.randrange(5, 14)) for _ in range(3))
        radius = F(1, 8)
        a, b = [tuple(tuple(v+sign*radius for v in point) for sign in (-1, 1)) for point in (p, q)]
        solids = []
        while len(solids) < 6:
            lo = tuple(F(rng.randrange(-10, 11)) for _ in range(3))
            hi = tuple(v+rng.randrange(1, 8) for v in lo)
            if not segment_intersects(p, q, (lo, hi)):
                solids.append((lo, hi))
        result = certify_occlusion(a, b, solids, depth=0, refinement=4)
        assert result['status'] == 'unresolved', result
        clear_cases += 1
    print(f'{clipping_cases} independent exact polygon checks, {checked+1} basic and {refined_cases} refined occlusion controls, '
          f'{clear_cases} continuous clear-ray controls and resource/certificate guards passed')


if __name__ == '__main__':
    main()

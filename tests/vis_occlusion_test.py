"""Independent controls for rational VIS interior-occlusion certificates.
SPDX-License-Identifier: GPL-3.0-or-later.
"""
from fractions import Fraction as F
from itertools import permutations, product
import random
from vis_occlusion import Budget, area2, box, certify_occlusion, clip, hull, reference_clip, replay_certificate


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
    print(f'{clipping_cases} independent exact polygon checks, {checked+1} visibility/occlusion controls and resource/certificate guards passed')


if __name__ == '__main__':
    main()

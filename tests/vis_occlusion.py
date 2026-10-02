"""Exact, bounded interior-occlusion certificates for axis-aligned VIS fixtures.

This is an independent qualification oracle, not a general BSP visibility solver.
An unresolved result says nothing about actual visibility. Zero-area grazing rays
are excluded; an occluded result certifies absence of an open set of sightlines
between the two box interiors. No floating-point tolerances or sampled rays enter
the certificate. SPDX-License-Identifier: GPL-3.0-or-later.
"""
from dataclasses import dataclass
from fractions import Fraction as F


class Limit(Exception):
    pass


@dataclass
class Budget:
    remaining: int = 2_000_000
    states: int = 2048
    used: int = 0

    def spend(self, count=1):
        if count > self.remaining:
            raise Limit('work_limit')
        self.remaining -= count
        self.used += count


def box(value):
    if len(value) != 2 or any(len(p) != 3 for p in value):
        raise ValueError('Expected two three-dimensional box corners')
    result = tuple(tuple(F(v) for v in p) for p in value)
    if any(a >= b for a, b in zip(*result)):
        raise ValueError('Expected positive-volume boxes')
    if any(abs(v) > 2**50 or v.denominator.bit_length() > 128 for p in result for v in p):
        raise ValueError('Box coordinates exceed oracle limits')
    return result


def area2(poly):
    return sum(p[0]*q[1]-p[1]*q[0] for p, q in zip(poly, poly[1:]+poly[:1]))


def clip(poly, half, budget):
    """Clip a convex line-parameter polygon against a*m+b*c+d >= 0."""
    budget.spend(len(poly))
    if not poly:
        return ()
    distances = [half[0]*p[0]+half[1]*p[1]+half[2] for p in poly]
    if min(distances) >= 0:
        return poly
    if max(distances) < 0:
        return ()
    out = []
    for i, p in enumerate(poly):
        q = poly[(i+1) % len(poly)]
        dp, dq = distances[i], distances[(i+1) % len(poly)]
        if dp >= 0:
            out.append(p)
        if (dp < 0 < dq) or (dq < 0 < dp):
            out.append(tuple((p[j]*dq-q[j]*dp)/(dq-dp) for j in range(2)))
    return tuple(dict.fromkeys(out))


def hull(points):
    """Independent exact monotone hull, used by the certificate replay."""
    points = sorted(set(points))
    def cross(a, b, c):
        return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    sides = []
    for order in (points, list(reversed(points))):
        side = []
        for p in order:
            while len(side) > 1 and cross(side[-2], side[-1], p) <= 0:
                side.pop()
            side.append(p)
        sides.extend(side[:-1])
    return tuple(sides)


def reference_clip(poly, half, budget):
    # Enumerate *all* vertex-pair intersections, including diagonals, then take
    # their convex hull. This does not share the edge-walk clipper's topology.
    budget.spend(len(poly)**2)
    values = [half[0]*p[0]+half[1]*p[1]+half[2] for p in poly]
    points = [p for p, d in zip(poly, values) if d >= 0]
    for i, p in enumerate(poly):
        for j in range(i):
            dp, dq = values[i], values[j]
            if (dp < 0 < dq) or (dq < 0 < dp):
                points.append(tuple((p[k]*dq-poly[j][k]*dp)/(dq-dp) for k in range(2)))
    return hull(points)


def projection(a, b, axis, side, budget, clipping):
    # q(t)=m*t+c must intersect each endpoint box's 2D projection. The positive
    # and negative slope cases have different minimum/maximum corners. A valid
    # 3D ray necessarily satisfies both independent projections; the converse
    # need not hold because their intersections can occur at different t.
    gap = b[0][axis]-a[1][axis]
    bound = max(abs(p-q) for p in (a[0][side], a[1][side])
                for q in (b[0][side], b[1][side]))/gap+1
    offset = max(abs(c[i][side])+bound*abs(c[i][axis])
                 for c in (a, b) for i in (0, 1))+1
    states = []
    for sign in (1, -1):
        poly = ((-bound, -offset), (bound, -offset), (bound, offset), (-bound, offset))
        poly = clipping(poly, (sign, 0, 0), budget)
        for c in (a, b):
            low = c[0][axis] if sign > 0 else c[1][axis]
            high = c[1][axis] if sign > 0 else c[0][axis]
            poly = clipping(poly, (-low, -1, c[1][side]), budget)
            poly = clipping(poly, (high, 1, -c[0][side]), budget)
        if area2(poly):
            states.append(poly)
    return states


def initial(a, b, axis, budget, clipping):
    if a[1][axis] > b[0][axis]:
        a, b = b, a
    if a[1][axis] >= b[0][axis]:
        return None
    sides = [i for i in range(3) if i != axis]
    x = projection(a, b, axis, sides[0], budget, clipping)
    y = projection(a, b, axis, sides[1], budget, clipping)
    return a, b, sides, [(p, q) for p in x for q in y]


def cut_states(states, cut, lo, hi, sides, budget, clipping):
    out = []
    for x, y in states:
        # Partition the complement of the opaque cross-section into four
        # disjoint interiors. Overlapping outside tests cause exponential copies.
        left = clipping(x, (-cut, -1, lo[sides[0]]), budget)
        right = clipping(x, (cut, 1, -hi[sides[0]]), budget)
        middle = clipping(clipping(x, (cut, 1, -lo[sides[0]]), budget),
                          (-cut, -1, hi[sides[0]]), budget)
        if area2(left):
            out.append((left, y))
        if area2(right):
            out.append((right, y))
        if area2(middle):
            for half in ((-cut, -1, lo[sides[1]]), (cut, 1, -hi[sides[1]])):
                clipped = clipping(y, half, budget)
                if area2(clipped):
                    out.append((middle, clipped))
        if len(out) > budget.states:
            raise Limit('state_limit')
    return out


def sweep(a, b, obstacles, axis, budget):
    start = initial(a, b, axis, budget, clip)
    if start is None:
        return None
    a, b, sides, states = start
    cuts = []
    for index, (lo, hi) in enumerate(obstacles):
        for position, cut in enumerate((lo[axis], (lo[axis]+hi[axis])/2, hi[axis])):
            # The closed gap boundaries are valid: box *interiors* lie strictly
            # on opposite sides, including when an occluder touches a box face.
            if not a[1][axis] <= cut <= b[0][axis]:
                continue
            updated = cut_states(states, cut, lo, hi, sides, budget, clip)
            if updated != states:
                cuts.append([index, position])
            states = updated
            if not states:
                return dict(axis=axis, cuts=cuts)
    return None


def partition(a, b, axis, chosen):
    cell = (a, b)[chosen]
    midpoint = (cell[0][axis]+cell[1][axis])/2
    left, right = [list(p) for p in cell], [list(p) for p in cell]
    left[1][axis] = right[0][axis] = midpoint
    return [(box(child), b) if chosen == 0 else (a, box(child)) for child in (left, right)]


def certify_occlusion(first, second, occluders, *, work=2_000_000, states=2048, depth=10):
    a, b = box(first), box(second)
    obstacles = tuple(box(o) for o in occluders)
    if not 0 <= depth <= 16 or not 1 <= work <= 100_000_000 or not 1 <= states <= 8192 or len(obstacles) > 256:
        raise ValueError('Invalid occlusion-oracle limit')
    budget = Budget(work, states)
    def prove(a, b, level):
        budget.spend()
        for axis in range(3):
            proof = sweep(a, b, obstacles, axis, budget)
            if proof is not None:
                return proof
        if level == depth:
            return None
        axis = max(range(3), key=lambda i:max(a[0][i]-b[1][i], b[0][i]-a[1][i]))
        if max(a[0][axis]-b[1][axis], b[0][axis]-a[1][axis]) <= 0:
            return None
        chosen = int(a[1][axis]-a[0][axis] < b[1][axis]-b[0][axis])
        children = []
        for left, right in partition(a, b, axis, chosen):
            proof = prove(left, right, level+1)
            if proof is None:
                return None
            children.append(proof)
        return dict(split_axis=axis, split_cell=chosen, children=children)
    try:
        proof = prove(a, b, 0)
        return dict(status='occluded_interiors' if proof is not None else 'unresolved',
                    work=budget.used, certificate=proof)
    except Limit as error:
        return dict(status=str(error), work=budget.used, certificate=None)


def replay_certificate(first, second, occluders, proof):
    """Replay a generated certificate with independent vertex-pair clipping."""
    obstacles = tuple(box(o) for o in occluders)
    budget = Budget(100_000_000, 8192)
    def verify(a, b, proof, depth):
        if not isinstance(proof, dict) or depth > 16:
            return False
        if 'split_axis' in proof:
            axis, chosen = proof['split_axis'], proof['split_cell']
            if axis not in range(3) or chosen not in (0, 1) or len(proof['children']) != 2:
                return False
            return all(verify(left, right, child, depth+1)
                       for (left, right), child in zip(partition(a, b, axis, chosen), proof['children']))
        axis = proof['axis']
        if axis not in range(3):
            return False
        start = initial(a, b, axis, budget, reference_clip)
        if start is None:
            return False
        a, b, sides, states = start
        for index, position in proof['cuts']:
            if not 0 <= index < len(obstacles) or position not in (0, 1, 2):
                return False
            lo, hi = obstacles[index]
            cut = (lo[axis], (lo[axis]+hi[axis])/2, hi[axis])[position]
            if not a[1][axis] <= cut <= b[0][axis]:
                return False
            states = cut_states(states, cut, lo, hi, sides, budget, reference_clip)
        return not states
    try:
        return verify(box(first), box(second), proof, 0)
    except (KeyError, TypeError, ValueError, IndexError, Limit):
        return False

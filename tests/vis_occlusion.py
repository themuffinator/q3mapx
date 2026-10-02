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


def slope_sign(poly):
    if all(m >= 0 for m, _ in poly) and any(m > 0 for m, _ in poly):
        return 1
    if all(m <= 0 for m, _ in poly) and any(m < 0 for m, _ in poly):
        return -1
    raise ValueError('Expected a positive-area polygon with a fixed slope sign')


def misses_endpoint(state, cell, sides, direction, budget):
    """An entry slab lies beyond another exit slab for the entire product."""
    x, y = state[direction], state[1-direction]
    sx, sy = slope_sign(x), slope_sign(y)
    entry = cell[0 if sx > 0 else 1][sides[direction]]
    leave = cell[1 if sy > 0 else 0][sides[1-direction]]
    budget.spend(len(x)*len(y))
    # This is bilinear in the two independent polygon parameters. Its extrema
    # occur at vertex pairs. Equality excludes only boundary/grazing lines.
    return all(sx*sy*((entry-cx)*my-(leave-cy)*mx) >= 0
               for mx, cx in x for my, cy in y)


def crosses_box(state, cell, axis, sides, budget):
    """All vertex-pair rays meet a closed blocker, including its full depth."""
    for poly in state:
        slope_sign(poly)
    for x in state[0]:
        for y in state[1]:
            budget.spend(3)
            low, high = cell[0][axis], cell[1][axis]
            for side, (m, c) in zip(sides, (x, y)):
                if not m:
                    if not cell[0][side] <= c <= cell[1][side]:
                        return False
                    continue
                entry, leave = sorted(((cell[0][side]-c)/m, (cell[1][side]-c)/m))
                low, high = max(low, entry), min(high, leave)
                if low > high:
                    return False
    # Fixed slope signs make the slab-overlap inequalities linear or bilinear
    # on this product. Checking every vertex pair bounds each inequality over
    # its whole interior; this is not a sampled-ray claim.
    return True


def gap_box(cell, a, b, axis):
    lo, hi = list(cell[0]), list(cell[1])
    lo[axis], hi[axis] = max(lo[axis], a[1][axis]), min(hi[axis], b[0][axis])
    return (lo, hi) if lo[axis] < hi[axis] else None


def parameter_children(state, side, anchor, a, b, axis, budget, clipping):
    t = (a[1][axis], b[0][axis])[anchor]
    values = [m*t+c for m, c in state[side]]
    midpoint = (min(values)+max(values))/2
    children = []
    for sign in (1, -1):
        child = list(state)
        child[side] = clipping(child[side], (sign*t, sign, -sign*midpoint), budget)
        children.append(child)
    return children


def refine_product(state, a, b, obstacles, axis, sides, budget, depth):
    budget.spend()
    for endpoint, cell in enumerate((a, b)):
        for direction in (0, 1):
            if misses_endpoint(state, cell, sides, direction, budget):
                return dict(outside_endpoint=endpoint, direction=direction)
    for index, obstacle in enumerate(obstacles):
        cell = gap_box(obstacle, a, b, axis)
        if cell is not None and crosses_box(state, cell, axis, sides, budget):
            return dict(inside_occluder=index)
    if not depth:
        return None
    choices = []
    for side, poly in enumerate(state):
        for anchor, t in enumerate((a[1][axis], b[0][axis])):
            values = [m*t+c for m, c in poly]
            choices.append((max(values)-min(values), side, anchor))
    width, side, anchor = max(choices)
    if not width:
        return None
    proofs = []
    for child in parameter_children(state, side, anchor, a, b, axis, budget, clip):
        if not area2(child[side]):
            return None
        proof = refine_product(child, a, b, obstacles, axis, sides, budget, depth-1)
        if proof is None:
            return None
        proofs.append(proof)
    return dict(split_side=side, anchor=anchor, children=proofs)


def sweep(a, b, obstacles, axis, budget, refinement=0):
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
    if refinement:
        proofs = []
        for state in states:
            proof = refine_product(state, a, b, obstacles, axis, sides, budget, refinement)
            if proof is None:
                return None
            proofs.append(proof)
        return dict(axis=axis, cuts=cuts, refinements=proofs)
    return None


def partition(a, b, axis, chosen):
    cell = (a, b)[chosen]
    midpoint = (cell[0][axis]+cell[1][axis])/2
    left, right = [list(p) for p in cell], [list(p) for p in cell]
    left[1][axis] = right[0][axis] = midpoint
    return [(box(child), b) if chosen == 0 else (a, box(child)) for child in (left, right)]


def certify_occlusion(first, second, occluders, *, work=2_000_000, states=2048, depth=10, refinement=12):
    a, b = box(first), box(second)
    obstacles = tuple(box(o) for o in occluders)
    if (not 0 <= depth <= 16 or not 0 <= refinement <= 16 or not 1 <= work <= 100_000_000
            or not 1 <= states <= 8192 or len(obstacles) > 256):
        raise ValueError('Invalid occlusion-oracle limit')
    budget = Budget(work, states)
    def prove(a, b, level):
        budget.spend()
        for axis in range(3):
            proof = sweep(a, b, obstacles, axis, budget)
            if proof is not None:
                return proof
        # Keep inexpensive direct certificates first on every axis. Only then
        # refine the conservative product of independent projection constraints.
        if refinement:
            for axis in range(3):
                proof = sweep(a, b, obstacles, axis, budget, refinement)
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
    """Replay with independent clipping and division-free slab inequalities."""
    obstacles = tuple(box(o) for o in occluders)
    budget = Budget(100_000_000, 8192)

    def bounds(point, cell, axis, sides, signs):
        # entry/denominator and exit/denominator, with nonnegative denominators.
        # At a zero slope, comparisons with the axial interval check that the
        # constant coordinate lies inside the slab, without dividing by zero.
        result = [(cell[0][axis], cell[1][axis], F(1))]
        for (m, c), side, sign in zip(point, sides, signs):
            entry = sign*(cell[0 if sign > 0 else 1][side]-c)
            leave = sign*(cell[1 if sign > 0 else 0][side]-c)
            result.append((entry, leave, sign*m))
        return result

    def verify_refinement(state, a, b, axis, sides, proof, depth=0):
        budget.spend()
        if not isinstance(proof, dict) or depth > 16 or any(not area2(p) for p in state):
            return False
        signs = [slope_sign(p) for p in state]
        if 'outside_endpoint' in proof:
            endpoint, direction = proof['outside_endpoint'], proof['direction']
            if endpoint not in (0, 1) or direction not in (0, 1):
                return False
            cell = (a, b)[endpoint]
            for x in state[0]:
                for y in state[1]:
                    budget.spend(3)
                    slab = bounds((x, y), cell, axis, sides, signs)
                    entry, _, denominator = slab[1+direction]
                    _, leave, other = slab[2-direction]
                    if entry*other < leave*denominator:
                        return False
            return True
        if 'inside_occluder' in proof:
            index = proof['inside_occluder']
            if not 0 <= index < len(obstacles):
                return False
            cell = gap_box(obstacles[index], a, b, axis)
            if cell is None:
                return False
            for x in state[0]:
                for y in state[1]:
                    budget.spend(9)
                    slab = bounds((x, y), cell, axis, sides, signs)
                    # Every entry bound must precede every exit bound. Each
                    # inequality is bilinear (or reduces to linear), so vertex
                    # pair extrema cover the entire convex product.
                    if any(entry*other > leave*denominator for entry, _, denominator in slab
                           for _, leave, other in slab):
                        return False
            return True
        side, anchor = proof['split_side'], proof['anchor']
        if side not in (0, 1) or anchor not in (0, 1) or len(proof['children']) != 2:
            return False
        children = parameter_children(state, side, anchor, a, b, axis, budget, reference_clip)
        return all(verify_refinement(child, a, b, axis, sides, p, depth+1)
                   for child, p in zip(children, proof['children']))

    def verify(a, b, proof, depth):
        budget.spend()
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
        if 'refinements' in proof:
            proofs = proof['refinements']
            return len(states) == len(proofs) and all(
                verify_refinement(state, a, b, axis, sides, p) for state, p in zip(states, proofs))
        return not states
    try:
        return verify(box(first), box(second), proof, 0)
    except (KeyError, TypeError, ValueError, IndexError, Limit):
        return False

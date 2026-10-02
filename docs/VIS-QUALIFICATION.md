# Geometric qualification of VIS merge differences

The current audit explains 83 of 84 distinct directed cluster pairs dropped by
existing merge options on two generated maps. Exact rational certificates prove
that these pairs have no open set of sightlines between their cell interiors.
One pair remains unresolved. This qualifies neither automatic merging nor
general engine visibility, including boundary and grazing cases.

A baseline PVS is a conservative estimate. Losing one of its bits does not by
itself demonstrate false culling, but a failed ray search does not demonstrate
occlusion either. Keep the baseline-inclusion gate until a separate geometric
argument establishes the applicable stronger contract. This audit records that
argument where available and retains the unresolved cases explicitly.

## Fixture and proof premises

`tests/vis_merge_qualification.py` builds the original grid=5 and grid=9
structural-pillar fixtures through the native compiler. It reads axial planes and
world BSP paths to reconstruct positive-volume cell boxes within the known source
room. It does not use stored leaf bounds, PRT polygons or PVS bits as geometry.
Every path box belonging to either cluster participates in the pair check.

Occluders are the opaque pillar brushes in the original source fixture:
`(x-20, y-20, 0)` to `(x+20, y+20, 80+(x+y)%112)`, at 128-unit grid intervals.
The inline door is omitted because it can open. The enclosing walls are also
unnecessary for interior-to-interior rays. The source, input BSP and PRT hashes
bind each result to its generated fixture. This is an oracle for these known
axis-aligned solids, not an arbitrary-map material or brush reconstruction tool.

`tests/vis_occlusion.py` uses Python's exact `Fraction` arithmetic:

1. Select an axis with a strictly positive gap between the endpoint boxes. A
   line has the form `q(t) = m*t + c` in each of the two remaining coordinates.
   Derive bounded convex polygons of line parameters intersecting each box's
   two-dimensional projection, separately for positive and negative slopes.
2. Take products of these polygons. They include every eligible 3D line and may
   include additional lines because the two projections can meet a box at
   different positions along the selected axis. This overestimate is conservative.
3. At a pillar's low face, middle or high face within the closed gap, a clear
   line must pass outside its opaque rectangular cross-section. Partition that
   complement into left, right, middle-below and middle-above regions with disjoint
   interiors. Clip the parameter polygons exactly and retain every positive-area
   product. Gap endpoints are valid cuts: the endpoint box interiors lie strictly
   on opposite sides even when a pillar touches a box face.
4. An empty collection certifies interior occlusion. Otherwise, bounded bisection
   can partition an endpoint box. Both children must certify. A remaining state,
   depth limit or exhausted work/state budget produces an unproven result.

There are no floating-point epsilons or sampled rays in this proof. Removing only
zero-area parameter polygons excludes grazing/boundary lines from its contract;
it does not establish that an engine can safely omit every associated boundary
surface or pixel. Bisection partition boundaries have the same limitation.

Certificates store selected axes, effective brush cross-sections and bisections.
Replay uses all vertex-pair intersections followed by an exact convex hull, rather
than the generator's edge-walk clipper. It shares the line-space formulation and
partition rules, so it is an independent clipping implementation, not an entirely
independent proof of the geometric model. Controls include axis permutations,
reflections, large translations, reversed endpoints, joined blockers and explicit
clear windows as narrow as `2^-80`.

Default proof limits are two million charged operations, 2,048 product states and
ten bisection levels per box pair. Input coordinate, denominator and obstacle
counts are bounded. These work counters are not wall-time or peak-memory bounds.
Replay has its own budget; rejected replay fails the audit. No budget failure is
converted to a positive certificate.

## Native matrix and current result

Each platform runs four solvers (full, portal-only, passage-only and fast), four
selections (unmerged, combined merge, polygon-only merge and combined merge with
sorting disabled), two fixtures and one/four workers: 64 native VIS commands.
Every solver is compared with its own unmerged control. The harness requires
identical VIS bytes across workers, unchanged BSP lumps 1–15 and PRT bytes,
correct self bits and zero padding. The entity lump is outside this preservation
check because normal compiler command metadata can change there.

| Fixture | Clusters | Distinct dropped pairs across modes | Certified interiors | Unresolved |
| --- | ---: | ---: | ---: | ---: |
| grid=5 | 84 | 11 | 11 | 0 |
| grid=9 | 220 | 73 | 72 | 1 |

Windows and Linux agree on all VIS bytes, dropped pairs and certificates. Their
input BSP/PRT file bytes differ, so each platform records its own input hashes.
The default full solver's combined merge still drops two baseline bits on grid=5
and 27 on grid=9; both small-fixture pairs and 26 large-fixture pairs have the
limited geometric certificate. Polygon-only merging retains the baseline rows
on these fixtures. None of these observations establish general merge safety.

The unresolved directed pair is grid=9 **79 → 217**:

- Source box: `(236, -276, 80)` to `(276, -236, 176)`.
- Target box: `(-492, -532, 0)` to `(-404, -492, 80)`.

It has neither an accepted occlusion certificate nor a demonstrated clear
sightline. It remains a failed qualification condition, without an exception or
relaxed tolerance. Further work must resolve this geometry and extend the
contract to boundary behavior, general protected portal topology and runtime
visibility cost before automatic regional transformations can be offered.

## Reproduction

The rational controls are ordinary CTest and need Python only:

```sh
ctest --test-dir build/release -R '^vis_occlusion$' --output-on-failure
```

The native qualification command is separate because its current result is
unresolved. Run from the repository root with the native compiler on its usual
runtime dependency path:

```sh
python tests/vis_merge_qualification.py --compiler build/release/bin/q3mapx.exe --work-dir build/release/tests/vis-merge-qualification
python3 tests/vis_merge_qualification.py --compiler build/linux-release/bin/q3mapx --work-dir build/linux-release/tests/vis-merge-qualification
```

Use the command for the desired platform. It writes `validation.json`, per-grid
`qualification.json`, generated source/BSP/PRT files and native command logs under
the explicit work directory. The JSON retains every lost pair, endpoint boxes,
proof status and certificate, including failures. The current command exits
nonzero after writing its report; a shell pipeline must preserve that status.
Running under Python optimization (`-O`) is unsupported because test invariants
use assertions. Native fixture outputs stay in the work directory; compiler
defaults remain unchanged.

See [recorded validation](validation/vis-merge-qualification.json) for binary and
source hashes, native counts and evidence paths. Timings are diagnostic only;
this round changes no compiler algorithm and claims no performance improvement.

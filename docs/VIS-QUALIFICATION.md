# Geometric qualification of VIS merge differences

The current audit explains all 84 distinct directed cluster pairs dropped by
existing merge options on two generated maps. Exact rational certificates prove
that these pairs have no open set of sightlines between their cell interiors.
This qualifies the known-source interior check only. Automatic merging and
general engine visibility, including boundary and grazing cases, remain unqualified.

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
4. An empty collection certifies interior occlusion. Otherwise, bounded refinement
   checks coupled 3D endpoint constraints and the full depth of blockers, then
   partitions line-parameter polygons when needed. Endpoint-box bisection is also
   available. Both children of every partition must certify. A remaining state,
   depth limit or exhausted work/state budget produces an unproven result.

### Coupled endpoint and blocker constraints

Meeting both two-dimensional projections is necessary but insufficient to meet
an endpoint box: the coordinate intervals might occur at different positions
along the line. Similarly, passing outside a blocker's three cut rectangles does
not establish that the line avoids the solid between those cuts.

The refinement uses exact slab intervals for each coordinate. An interval's
entry must precede every other coordinate's exit for the line to meet the 3D box.
Each polygon retains a fixed slope sign, so cross-multiplication makes these
conditions linear or bilinear in the two independent parameter pairs. A bilinear
function on a product of convex polygons is a convex combination of its values
at all vertex pairs: `f(p,q) = sum_i sum_j alpha_i*beta_j*f(v_i,w_j)`.
Its extrema are therefore bounded by those vertex values.

This gives two exact rejection rules for an entire product of polygons:

- One endpoint entry is at or beyond another endpoint exit at every vertex pair:
  no open set of these lines meets that endpoint's interior.
- Every entry precedes every exit of one opaque box at every vertex pair: all
  these lines meet that blocker. First clip the box's extent to the gap between
  endpoint boxes, ensuring that the obstruction lies between them.

Vertex pairs establish extrema of explicit inequalities; they are not ray
samples standing in for the rest of the polygon. When neither rule applies,
bisect a polygon by its coordinate at one gap endpoint. Exact half-plane clipping
retains both children, whose union covers the original product. Choose the widest
coordinate range to refine, with deterministic ties. The certificate records the
chosen side/anchor and the rule justifying each leaf. No heuristic choice alone
can certify occlusion.

There are no floating-point epsilons or sampled rays in this proof. Removing only
zero-area parameter polygons excludes grazing/boundary lines from its contract;
it does not establish that an engine can safely omit every associated boundary
surface or pixel. Bisection partition boundaries have the same limitation.

Certificates store selected axes, effective brush cross-sections and bisections.
Replay uses all vertex-pair intersections followed by an exact convex hull, rather
than the generator's edge-walk clipper. For refined leaves it uses division-free
slab inequalities rather than the generator's ray/box interval intersections.
It shares the line-space formulation and partition rules; it does not independently
prove the geometric model. Controls include axis permutations, reflections, large
translations, reversed endpoints, joined blockers, explicit clear windows as
narrow as `2^-80` and 80 independently checked continuous clear segments.

Default proof limits are two million charged operations, 2,048 product states and
ten endpoint-box bisection levels plus twelve parameter-refinement levels per
product. Input coordinate, denominator and obstacle counts are bounded. These
work counters are not wall-time or peak-memory bounds.
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
| grid=9 | 220 | 73 | 73 | 0 |

Windows and Linux agree on all VIS bytes, dropped pairs and certificates. Their
input BSP/PRT file bytes differ, so each platform records its own input hashes.
The default full solver's combined merge still drops two baseline bits on grid=5
and 27 on grid=9; both small-fixture pairs and all 27 large-fixture pairs have the
limited geometric certificate. Polygon-only merging retains the baseline rows
on these fixtures. None of these observations establish general merge safety.

The formerly unresolved directed pair is grid=9 **79 → 217**:

- Source box: `(236, -276, 80)` to `(276, -236, 176)`.
- Target box: `(-492, -532, 0)` to `(-404, -492, 80)`.

The original cut-only oracle retained two possible line-parameter products here.
Coupled refinement proves both empty of interior sightlines, using 56 proof nodes
with twelve levels of splitting at most. Eighteen terminal nodes reject impossible
endpoint combinations; eleven certify intersection with the pillar centered at
`(-128, -384)`, whose top is `z=128`. Every leaf replays independently. A reduced
four-blocker regression checks this result through all axis permutations,
reflections and endpoint directions, including a large translation.

No pair is exempted and no numerical tolerance is relaxed. The broader contract
still needs boundary behavior, general protected portal topology and runtime
visibility cost before automatic regional transformations can be offered.

## Reproduction

Both rational controls and the native matrix are ordinary CTest. The rational
group needs Python only; the native group needs the built compiler:

```sh
ctest --test-dir build/release -R '^vis_(occlusion|merge_qualification)$' --output-on-failure
```

The native group has a 900-second timeout. It can also run as a standalone
command from the repository root with the native compiler on its usual runtime
dependency path:

```sh
python tests/vis_merge_qualification.py --compiler build/release/bin/q3mapx.exe --work-dir build/release/tests/vis-merge-qualification
python3 tests/vis_merge_qualification.py --compiler build/linux-release/bin/q3mapx --work-dir build/linux-release/tests/vis-merge-qualification
```

Use the command for the desired platform. It writes `validation.json`, per-grid
`qualification.json`, generated source/BSP/PRT files and native command logs under
the explicit work directory. The JSON retains every lost pair, endpoint boxes,
proof status, replay result and certificate, including failures. It records exact
oracle limits and original opaque source boxes. An unresolved pair or failed
replay makes the command exit nonzero; a shell pipeline must preserve that status.
Running under Python optimization (`-O`) is unsupported because test invariants
use assertions. Native fixture outputs stay in the work directory; compiler
defaults remain unchanged.

See [recorded validation](validation/vis-merge-qualification.json) for binary and
source hashes, native counts and evidence paths. Timings are diagnostic only;
this round changes no compiler algorithm and claims no performance improvement.

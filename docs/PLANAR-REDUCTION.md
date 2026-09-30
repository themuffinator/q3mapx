# Exact planar triangle reduction core

## Delivery boundary

`libs/q3mapx/planar_reduction.*` implements the first transformation core for
[M10](PLAN.md#m10--geometry-optimization-without-presentation-changes). It removes
redundant interior vertices and retriangulates their incident faces. It is a
library with independent tests. The subsequent [native optimizer](GEOMETRY-OPTIMIZATION.md)
now uses it through an explicit Quake3e OpenGL material/format contract in the CLI
and workbench. The mathematical contract below is a prerequisite for that adapter,
not a standalone claim of renderer equivalence. Ordinary compilation stays unchanged.

The inherited meta stage already deduplicates and groups compatible triangles.
This core goes further: it can remove tessellation from those compiled groups.
It does not weld vertices, merge surfaces, remove boundary vertices, simplify
collision, compact the vertex array or approximate curved geometry. Its greedy
ordering does not promise a globally minimal triangulation.

## Transformation and contract

The input is a vertex array and indexed triangles. Each vertex has three binary32
position coordinates followed by 29 scalar interpolants. A native adapter can
represent three normal components, two base UVs, four lightmap UV pairs and four
RGBA light styles. Missing native fields must be filled consistently. The core
does not assign renderer meaning to those values.

Candidates are ordered by current incident-face count, then original vertex ID.
For a vertex to be removed, all of the following must hold:

1. It is an interior vertex with a single, consistently oriented ring. Original
   boundary endpoints and vertices touching degenerate or nonmanifold triangles
   are protected. Disconnected or self-crossing rings are rejected.
2. Its incident triangles lie in exactly one plane, with nonzero projected area.
   Their projected boundary is a simple polygon and the center lies strictly on
   the interior side of each boundary edge. Concave, star-shaped rings are allowed.
3. Every scalar field is exactly affine in the chosen plane coordinates over the
   center and complete ring. A one-ULP geometry, UV, normal or color discontinuity
   is sufficient to reject a candidate. There is no near-planar/near-affine epsilon.
4. Ear clipping can triangulate the ring without skipping boundary segments or
   accepting a zero-area/flipped triangle. An ear containing another boundary
   vertex, including on its new diagonal, is rejected. Thus existing collinear
   boundary subdivisions and T-junction endpoints survive.
5. New internal diagonals do not already belong to faces outside the removed
   fan. This prevents introducing a nonmanifold edge through an otherwise locally
   valid replacement.

An accepted edit replaces `n` faces with `n - 2`, preserving every oriented
boundary segment, coverage and exact real-valued interpolation of the supplied
fields. No original vertex coordinate or attribute changes. The result contains
the surviving faces and replayable edits: removed vertex, removed stable face
IDs and new faces. Each new face receives the next consecutive history ID.
This is local preservation, not certification that arbitrary input meshes are
globally free from overlaps, cracks or other pre-existing defects.

## Arithmetic, bounds and determinism

`exact_predicates.*` returns exact signs of homogeneous 2D/3D determinants for
finite binary32 inputs. Two-float products are exact in binary64. For the
three-float products, an explicit fused multiply-add retains the multiplication
residual. Error-free sums accumulate the six or 48 product/residual terms in a
fixed 64-component expansion. Homogeneous coordinates avoid losing low bits by
subtracting positions with widely separated exponents before the determinant.
All product and residual exponents fit binary64 for the full finite binary32 range.

The implementation requires IEEE binary32/binary64, round-to-nearest arithmetic
and preservation of binary32 subnormals. It checks the rounding environment and
smallest subnormal, rejects nonfinite arguments, and refuses GCC/Clang fast-math
builds. Other toolchains must also retain strict floating-point semantics.
Finite-precision GPU interpolation can still differ after retriangulation; exact
geometric arithmetic does not establish pixel identity.

Defaults bound an invocation to 65,536 vertices, 131,072 input triangles, 524,288
history faces, 128 incident triangles per candidate and 20,000,000 work units.
Work units account for visits and weighted predicates, not CPU instructions,
elapsed time or memory usage. An oversized ring is skipped. Invalid data or
exhausted hard work/history/input capacity throws; immutable input remains
available and callers must not publish partial work. No compiler globals or
shared mutable state are used. Current calls are serial; future independent
surface jobs can use the existing pool with ordered result publication.

## Executed evidence

The [validation record](validation/planar-reduction.json) identifies sources,
binaries, commands and retained logs for Windows Release, Linux Release and
ASan/UBSan. The tests include:

- 20 flat/sloped grids in both windings, reaching their boundary-preserving
  minimum with 1,364 interior removals; 4,212 independent coverage/interpolation
  samples and a replay of every edit.
- 4,136 determinant comparisons against rational Gaussian elimination, covering
  the full binary32 exponent range, exact cancellation, signed zero, subnormals,
  dependent rows, permutations and one-ULP perturbations.
- 32 rational mesh oracles with arbitrary dyadic scales, skew, slope, holes,
  reversed winding and attribute seams; 607 exact interpolation samples.
- Concave and self-crossing rings, nonmanifold/duplicate faces, existing external
  diagonals, unsupported arithmetic and exact/exhausted capacity controls.
- Two original generated OBJ grids compiled through the actual Quake 3
  BSP/VIS/LIGHT pipeline, then inspected without rewriting the BSP.

The original core-only validation fixture (before the later winding correction)
remains 512 triangles after ordinary meta processing.
The compiler emits six grid surfaces of at most 64 vertices each; this
core preserves each surface's boundary and reduces their combined count to 146.
Those counts are mathematical candidates, before runtime-material checks:

| Fixture | Before LIGHT | After LIGHT | Candidate reduction after LIGHT |
| --- | ---: | ---: | ---: |
| Uniform ambient | 512 → 146 | 512 → 146 | 366 triangles (71.5%) |
| Point light | 512 → 146 | 512 → 496 | 16 triangles (3.1%) |

The point-light control preserves most vertices because the baked color field is
not affine. It demonstrates why an early topology edit cannot assume that later
lighting will reproduce the original vertex interpolation. These fixtures are
synthetic, asset-independent and contain no third-party map/model content. There
is no compiler-speed, frame-time or memory improvement claim from these counts.

All three builds produce identical rational-oracle reports and matching multisets
of native candidate summaries, including work/rejection counters and local output
indices. The inherited compiler orders surfaces differently between Windows and
Linux, so native surface IDs and complete input/BSP bytes are not equal. The
comparison explicitly excludes those IDs and does not claim cross-platform BSP
byte identity. The native fixtures add 276 rational interpolation samples.

## Remaining material, native-format and renderer gates

A source audit of id Software's [Quake III renderer](https://github.com/id-Software/Quake-III-Arena/blob/master/code/renderer/tr_shade.c)
found concrete constraints beyond BSP attributes. Its projected dynamic-light
pass computes a piecewise, quantized color factor at vertices from local Z
distance. Retriangulation can change that interpolated factor. The generic stage
checks `SURF_NODLIGHT`, while the specialized vertex-lit and lightmapped iterators
call the pass without that check; the flag alone is not a universal eligibility
proof. Local reference-file hashes and function/line locations are recorded in
the validation record. No renderer code was incorporated or modified.

Vertex-color overbright conversion can introduce quantization; fog, deformation,
environment texture generation, specular/portal alpha, turbulent UVs, blending,
shader remapping and other profile-specific effects can depend on vertices or
triangle order. Exact affine input normals are not proof that all derived shader
values stay affine. Horizontal faces may avoid the particular Z-distance issue,
but that alone does not approve their complete rendering behavior.

The subsequent [post-LIGHT native adapter](GEOMETRY-OPTIMIZATION.md) supplies
bounded material inventory, explicit eligibility/rejection reasons, index-only
publication, native preservation tests, worker parity and CLI/workbench workflows.
It targets Quake3e OpenGL and requires authored `nomarks` and `nodlight`; it does
not qualify the original renderer's specialized iterators. Its independent render
matrix and corrected fixture are recorded separately. Other orientations,
renderer/material profiles, native formats and the initial BSP-stage design remain
open. The older record above is retained as core-only evidence.

## Reproduce

Build `q3mapx` and `planar_reduction_test` with an existing project preset, then run:

```sh
ctest --test-dir build/release -R '^planar_reduction' --output-on-failure -V
python tests/planar_reduction_native.py \
  --compiler build/release/bin/q3mapx \
  --unit build/release/bin/planar_reduction_test \
  --work-dir build/release/tests/planar-reduction-native
```

Use `.exe` on Windows and the corresponding Linux build directory there. The
native harness records every surface's candidate count, rejection/work counters,
input/index hashes and read-only preservation checks. The low-level test driver's
`--mesh` and `--predicates` modes are test interfaces, not public compiler commands.

# Triangle-only patch reconstruction

MAP recovery can now reconstruct editable quadratic patches from eligible
triangle-only BSP meshes. It works without adjacent MAP/SRF files or a retained
source archive. It is an inference from compiled samples: different source
patches can produce the same rounded samples, and arbitrary triangles are not
necessarily a patch.

```sh
q3mapx -game quake3 -decompile -patch-recovery auto -patch-colors alpha -o recovered.map map.bsp
q3mapx -game quake3 -decompile -patch-recovery fit -patch-colors rgba -o fitted.map old.bsp
```

| Policy | Behavior |
| --- | --- |
| `none` (default) | Preserve the existing MAP exporter. |
| `source` | Require a verified [source archive](PATCH-SOURCE.md); restore retained controls/settings. |
| `fit` | Infer eligible triangle patches, regardless of whether an archive exists. |
| `auto` | Restore a verified archive when present, then fit remaining eligible triangles. An invalid/stale archive fails; it is not silently replaced by a guess. |

`-patch-colors none` (default) fits geometry/UV with opaque alpha and white source
RGB for rebaking. `alpha` recovers the stored alpha field and leaves RGB to
lighting. `rgba` fits all stored channels as material color; baked RGB can be
frozen into the result. Source-archive recovery always retains the archived mode
and settings. The native-patch `-patch-color-subdivisions` setting does not choose
fitted-patch subdivisions: those follow the observed sample grid. Density inherits
with an explicit zero override; the original density is not inferred.

## Evidence required for a fit

The adapter groups compatible surfaces by model, material, fog and light-style
semantics, welds matching XYZ/ST/requested-color samples and finds connected
components. Splits across BSP draw surfaces do not erase the candidate grid.
Both planar and triangle-soup surface types are considered.

A candidate needs one rectangular boundary in an affine UV frame, regular
sampling on both axes, exactly one sample per grid point, consistent winding,
complete two-triangle cells, and the compiler's alternating diagonals. Affine
rotation/shear of UVs is supported. Missing cells, holes, overlapping triangles,
nonmanifold seams, nonuniform sampling or incompatible triangulation are rejected.
Welds do not average differing requested colors or UVs across seams.

The fitter tries 32, 16, 8 and 4 segments per quadratic span, preferring the
coarsest verified control grid. At least five samples per axis are required.
Control dimensions must be odd, between 3 and 31, and satisfy the existing paint
tessellation budget. It verifies every XYZ/ST sample after float32 control
rounding. The per-coordinate allowance is the smaller of a fixed cap (0.001
world units, 0.00001 UV units) and `0.000001 + abs(sample) * 0.00000048`.
The report records actual maximum errors.

Requested color bytes must reproduce **every observed byte exactly**. Endpoint
and midpoint rounding intervals bound the possible control bytes. Bounded
backtracking makes neighboring spans agree on shared controls. Deterministic
selection of one valid field does not establish unique original control bytes.

Output uses `q3mapxPatchDef2` with observed subdivisions so that geometry and
channels use the compiler's continuous painted-patch evaluator. Comments and the
JSON report identify it as inferred. Collision is not synthesized: solid or
player-clipping materials are excluded. Materials shared with brushes or native
patches in the same model are also excluded conservatively to avoid duplicates.
Consequently some legitimate patches sharing those materials are skipped.

Current shader color/UV/geometry modifiers, redirects, incompatible paint
materials, surviving modifier volumes and conflicting active requested color
styles prevent fitting. Absence of such operations in current assets does not
prove their absence in the original build. Arbitrary meshes, heavily optimized
or decimated grids, missing/warped UVs, non-quadratic surfaces and low-sample grids
may have insufficient evidence. Rejected triangles retain the existing omission
from MAP export; no artificial patch is substituted.

## Bounds and reporting

`-patch-fit-work N` sets a shared budget for input visits and evaluated channel
samples, default 50,000,000, range 1–1,000,000,000. Exhausted candidates are
reported as `work_limit`; other accepted results remain available. This is a
fitting-work budget, not a bound on BSP parsing or all decompiler work.

Groups are capped at 262,144 stored vertices and 1,572,864 indices before welding;
components at 65,536 unique vertices and 131,072 triangles. Fitted output is capped
at 10,000 patches and 1,000,000 controls. The report retains up to 10,000 decisions
and 64 supporting surface IDs per decision, counting omissions. Counts and MAP
output remain complete for accepted results. Existing MAP/report publication
checks and the 64 MiB report limit apply.

`patch_recovery.triangle_fitting` records policy, work, accepted counts, each
candidate's supporting surfaces, rejection status, fitted dimensions/sampling and
errors. Original source and rebuild equivalence are explicitly unproven. Read
these decisions before treating an export as complete.

## Validation and remaining limits

```sh
ctest --test-dir build/release -R '^(patch_fit|patch_reconstruction|patch_source|patch_color_recovery|decompile_recovery|recovery_outputs|patch_paint)$' --output-on-failure -j 2
```

An independent long-double de Casteljau oracle covers shuffled vertex/triangle
order, affine UV frames, flat/curved multi-span grids and byte rounding. Generated
Q3/JA BSP tests remove archives and poison adjacent source files. Recompiled and
relit meshes are compared by oriented triangle positions, UVs and requested
channels. Legacy `patchDef2` and mixed archived/inferred recovery are covered.
Malformed topology, color/style conflicts, material replay, budget limits and
invalid-option output preservation have explicit controls.

The optional [engine harness](../tests/renderer/patch_source_render.py) accepts
`--fit` to compare archive-free fitted/rebuilt scenes against their source BSPs.
It uses registered engine screenshots, windowed SDL offscreen and disabled
input/network. Flat and curved neutral-material scenes match pixel-for-pixel.
See [platforms and evidence](validation/patch-reconstruction.json).

These checks do not prove identical author grouping, normals, inherited settings,
lightmap atlases, arbitrary shader lighting, dynamic effects or runtime LOD seams.
Finer geometry refinement or different compiler/material settings can change a
rebuild. Native GUI recovery controls and broader engine/game qualification
remain separate work. Original MAP files remain the authoritative source.

# BSP decompilation

## Intent

Recover editable maps from supported BSPs with better texture alignment, faithful
brush entities and patches, robust validation, and useful diagnostics. Preserve
the existing `-convert -format map` family of workflows and expose a convenient
decompile action in the CLI and workbench.

## Limits of the input format

A compiled BSP does not preserve the original editor grouping, all source brush
boundaries, source model instances, or removed entities. Decompilation cannot
promise an identical source map. Report which data was reconstructed, approximated,
or unavailable. New inference work will propose plausible detail flags, groups
and stripped lights, with evidence and uncertainty recorded separately from
surviving source metadata.

## Current foundation and planned inference

Validated native loaders, spatial texture matching, finite UV reconstruction,
brush-entity/origin/patch preservation and recovery reports are implemented below.
Further recovery performance work remains active.

The next fidelity work is specified in [recovery inference](RECOVERY-INFERENCE.md):

- Detail/structural classification using BSP and visibility/portal evidence, with
  explicit handling of missing or ambiguous VIS data.
- Inferred `func_group` assemblies that retain effective compile properties and
  entity ownership, independently of whether their brushes are detail.
- Point/spot light inference from baked lighting, accounting for sky/sun, surface
  emitters and indirect light; appropriate recovery of spotlight target links.
- More accurate brush/UV/patch reconstruction and CLI/workbench tools for reviewing
  evidence, applying manual corrections and comparing rebuilt geometry and lighting.

The first optional geometric detail and surface-supported group policies are
implemented below; broader grouping, light inference and authoring review remain
planned. Known-source
fixtures, held-out lighting samples and recorded uncertainty will distinguish a
close visual recreation from recovery of uniquely identifiable authoring data.
The separate [intelligent compiler options](COMPILER-OPTIMIZATION.md) optimize new
builds; they do not silently alter the decompiler's reconstruction objective.

## Usage

```sh
q3mapx -decompile -game quake3 -fs_basepath /path/to/game -o recovered.map maps/example.bsp
q3mapx -decompile -format map_bp -report recovery.json maps/example.bsp
q3mapx -decompile -brush-order rebuild -o recovered.map maps/example.bsp
q3mapx -decompile -detail-policy cells -brush-order rebuild -o candidate.map maps/example.bsp
q3mapx -decompile -group-policy surfaces -detail-policy cells -o assemblies.map maps/example.bsp
q3mapx -convert -format map_220 maps/example.bsp
```

`-decompile` defaults to Valve 220 output and writes `<output.map>.recovery.json`.
Without `-o`, the map is `<input>_converted.map`. `-report` selects a different JSON
path. The legacy `-convert` syntax remains available and writes a report when
explicitly requested or when cell detail or surface grouping is selected. `-o` requires a
`.map` extension and `-report` requires `.json`.
Unknown conversion formats/options now produce a diagnostic instead of silently
choosing ASE or ignoring the option.

Development after 0.3.0 stages the MAP and its report beside their destinations
using buffered, checked writes. Both must finish before either is published. A
report-path or write/close failure preserves existing outputs. The report is
published first; if MAP replacement then fails, the old report is restored, or
the newly created report is removed. Legacy conversion without a report uses
one atomic MAP replacement. Directories and symbolic links are rejected as output
destinations. A failed rollback identifies the retained original backup.

The MAP and report are separate filesystem names: process/power loss between
their replacements can leave different generations. Concurrent writers, metadata
preservation and power-loss durability are outside this guarantee. Fatal compiler
exits can leave identifiable `.q3mapx-*.tmp` siblings. The portable 0.3.0 archive
predates these protections.

Use the correct game profile and resource paths: shader dimensions affect texture
recovery. Valve 220 and brush primitives preserve affine texture mappings. Classic
Quake texture definitions cannot represent arbitrary shear; the report counts
such approximations. `-fast` skips texture reconstruction and reports fallback axes.
Development builds after 0.3.0 use the same detail-flag policy for fast and full
recovery in all three MAP formats. Earlier fast recovery wrote zero detail flags,
which could turn detail geometry into structural splitters when rebuilt.

### Brush order for rebuilding

`-brush-order bsp|rebuild` selects MAP brush order in development builds after
0.3.0. `bsp` is the default and retains the existing BSP-record export order.
`rebuild` reverses opaque brushes within each model while preserving translucent
brush order. This compensates for the q3mapx MAP loader prepending opaque brushes
and appending translucent brushes. Preserving compiled brush order can prevent
different tied splitter choices during a subsequent BSP build.

Opacity comes from the current shader definitions of exported sides, including
mixed-material brushes. Redundant sides with empty windings do not participate.
The mode supports fast/full recovery and all three MAP writers, including legacy
`-convert`. It requires a profile with BSP writing support and excludes `-wtf`,
which can replace materials. Native recovery-only profiles retain ordinary BSP
order; native round-trip fidelity is not implied. The workbench's **Project →
Recovery** tab saves this choice and checks the selected compiler's advertised
policy/profile support before allowing rebuild-order jobs.

Use the same shader assets and compatible compiler settings for recovery and
recompilation. This is an attempt to preserve compiled ordering, not recovery of
the author's editor sequence or a guarantee of equivalent BSP/VIS. Lost source
flags, plane/side ordering, other compiler differences and missing assets can
still affect the result. In particular, contradictory authored detail bits on
explicitly structural sides can change splitter priorities even though the brush
itself becomes structural; standard BSP brush data does not retain those numeric
source bits. The option does not invent them.

The optional version 1 `brush_order` report object records `policy`, `basis`,
`author_order_proven: false` and `rebuild_equivalence_proven: false`. The report
names shader/loader assumptions when rebuilding order is requested.

### Group inference policy

Development builds after 0.3.0 accept `-group-policy none|surfaces`. The default
`none` leaves recovered world geometry flat. `surfaces` proposes `func_group`
assemblies from brushes that overlap the same rendered BSP surface. It works
independently of detail classification: one group can contain structural and
detail brushes, and can use either detail policy. It supports fast/full recovery,
all three MAP formats and legacy `-convert`. A report is mandatory. It automatically
selects rebuild brush order; an explicit `-brush-order bsp` conflicts with it.
It requires compiled BSP input and a BSP-writing profile, excludes `-wtf`, and
is not yet exposed in the workbench.

The analysis matches current side materials and closely coplanar, positive-area
triangle overlap. Brushes linked through shared surfaces form deterministic
proposals of at least two members. A source compiler can merge several brushes
into one surface while respecting group boundaries, but ordinary world geometry
can produce the same evidence. Shared surfaces do **not** prove an original group,
its name or its parameters. Disconnected assemblies and surfaces split by the
compiler may leave insufficient evidence. This first policy does not invent
groups around unrelated brushes to fill those gaps.

Only world brushes with unambiguous ownership and usable exported windings can
move. Patches and brush entities stay in their recovered owners. Special
hint/skip/origin/portal/sky/liquid/fog materials, context-dependent shader effects,
and brushes touching the outer world bounds are excluded. The bounds exclusion
is a conservative scope heuristic, not a proof of sealing. Excess overlapping
surface coverage marks ambiguous support. World index maps, smoothing context,
nonstandard world entities and shared model ownership can block all export.
Excluded brushes remain part of association analysis so a linked proposal cannot
silently drop an incompatible member.

The MAP loader reverses opaque brushes when reading them and again when collapsing
a group. Export therefore requires each group's members to be contiguous within
each opacity class, with opaque groups forming a prefix of compiled opaque order
and translucent groups a suffix of translucent order. Conflicting group-order
constraints leave proposals flat. The report distinguishes exclusions,
noncontiguous order, world-order changes and incompatible opacity ordering.
These restrictions preserve the recoverable brush sequence; they do not guarantee
identical output from arbitrary recompilation settings.

Generated names are `q3mapx_inferred_group_N`. Groups copy the recovered worldspawn's
recognized shadow, lightmap scale/sample, cel shader, smoothing and ambient/color
keys so regrouping does not reset those baseline settings. This does not recover
discarded original group values. Rebuild and compare using the same current assets
and compiler settings before treating a proposal as a faithful reconstruction.

`-group-max-work N` bounds the additional evidence, surface association and ordering
work (default 50,000,000; range 1–100,000,000). Hard limits include 50,000 world
brushes, 256 sides per world brush, 2,000,000 expanded world triangles, 2,048 points
per winding, 2,000,000 total winding points and surface associations, and absolute
brush coordinates of 10,000,000. The budget does not bound all inherited loading,
shader processing or MAP export. Inference reports share the 64 MiB ceiling;
budget/limit failures preserve previous outputs.

The optional schema 1 `group_inference` object records the policy/basis, current
shader assumptions, copied compile keys, work usage, any global export block,
per-brush supporting surfaces/exclusion and every proposed group's members,
bounds, status, generated name, export flag and detail count. `emission_order`
maps accepted proposals to emitted entities. `source_entities` counts input
entities; the existing `entities` count includes generated groups. Original
grouping/parameters and rebuild validation are explicitly unproven. Saved manual
overrides, alternative partitions and GUI evidence review remain future work.

The [validation corpus](validation/recovery-groups.json) includes 42 rebuilds,
mixed detail/opacity, sloped faces, preserved brush entities/patches, and three
controlled lighting rebuilds. A flat-source case deliberately yields plausible
assemblies without original groups. A disconnected case deliberately stays flat
when export would change brush order. These limits are part of the checks.

### Detail inference policy

Development builds after 0.3.0 accept `-detail-policy legacy|cells`. The default
`legacy` retains the existing nonopaque leaf-reference classification heuristic.
`cells` adds bounded geometric evidence independently of those reference lists,
using [convex brush/tree intersections](BSP-EVIDENCE.md#brush-interiors). It supports
all three MAP formats, fast/full export and legacy `-convert`, and always writes
a recovery report. It requires a BSP-writing profile and excludes `-wtf` material
replacement. Native recovery-only profiles support read-only cell evidence, but
not this classification/export policy. The workbench does not yet expose it.

The policy starts from each brush's legacy flag. It only considers uniquely owned
world geometry with successful cell analysis and usable exported windings. It
combines the actual exported sides' current shader compile flags, omitting empty
redundant sides. Explicit structural material semantics clear the detail bit;
hint/skip, area/antiportals, origin, sky, liquid and fog semantics preserve the
baseline. Explicit detail materials then request detail; nonsolid/translucent
materials otherwise preserve the baseline. For remaining opaque solids, a point
strictly inside an open leaf is a detail candidate. A PVS table with missing
self-visibility bits suppresses that geometric promotion.

This follows the compiler's behavior of marking leaf space opaque with structural
opaque brushes, while detail brushes are inserted without doing so. It repairs
missing leaf references and checks structural semantics beyond the first shader.
It does not prove which brush caused a partition. Hidden detail inside opaque
space remains ambiguous: absent open witnesses preserve the legacy flag rather
than forcing structural. Thin fragments, unsupported enclosures, shared trees and
resource limits similarly preserve the baseline. Brush entities retain their
previous flags. Missing VIS remains unknown; all-visible or fast VIS does not
establish authoring intent. Current shader assets may differ from the original
assets, so material-based decisions are assumptions recorded in the report.

`-detail-max-work N` selects the geometry/PVS analysis budget (default 50,000,000;
range 1–100,000,000). It applies to this optional analysis, not all preexisting
loading, shader processing or MAP export work. Exhaustion occurs before output
staging, preserving previous MAP/report files. Geometry scratch limits and
numerical thresholds are those of `-bsp-evidence -brush-cells`. Optional inference
reports are limited to 64 MiB; exceeding that limit preserves prior outputs too.

The schema 1 report adds an optional `detail_inference` object. It records policy,
analysis work, VIS availability/inconsistency and one decision per normalized
input brush: baseline/applied flags, a reason, evaluated material compile flags
(null when unevaluated), cell status, fragment counts, interior clusters, invisible
PVS pairs and an open witness where found. These records include skipped input
brushes; `detail_classification` separately counts brushes actually exported.
Reports explicitly mark original classification, original shader identity and
rebuilt BSP equivalence as unproven. This is a deterministic proposed export,
not yet a saved-override or interactive review system.

The [known-source corpus](validation/brush-cell-inference.json) checks 36 rebuilds
in fast/full mode across three formats. Controls include removed detail references,
mixed groups, translucent structural materials and structural semantics present
only on a non-first side. They require source geometry/materials/contents/order,
entity preservation and sampled spatial partition/PVS agreement. These synthetic
controls support the option's limited policy; they do not establish universal
classification accuracy or recreate lost author metadata.

## Recovery details

A per-material bounds hierarchy searches all overlapping triangles, including
triangles larger than the source brush face. The previous maximum-bound cutoff
could miss these. Degenerate triangles are excluded. The affine solve works on
edge differences in double precision, rejects ill-conditioned geometry and
constant/non-finite UV axes, and supplies finite fallback transforms. Current
default brush-detail membership uses nonopaque leaf references and explicit
structural shader flags. The optional cell policy above extends this heuristic;
portal adjacency and authoring review remain future work.

Fast export now solves nonaxial face points from stored planes in double precision
and prints 17 significant digits. Its earlier float tangent basis and three
decimal places could change sloped brush planes during rebuilding. Axial point
formatting remains unchanged. Controlled slopes in all six dominant-axis/sign
orientations now retain exact stored brush planes; arbitrary original editor
coordinates are still unrecoverable, and fast mode still uses fallback texture axes.

The optional `detail_classification` object in version 1 reports names this
selected policy, records its structural override scope (`brush_shader_contents`
by default, extended to current exported side materials in cell mode), and
counts exported brushes with/without the detail bit. `author_classification_proven`
is false: neither a retained flag nor leaf membership proves the original editor
choice. Counts include brush entities and exclude synthetic origin brushes;
brush-model leaf references can mark entity geometry as detail without identifying
the author's source flag. This ambiguity remains work for the planned inference
and provenance system.

The version 1 JSON report records entities, exported/skipped brushes, patches,
matched/fallback faces, degenerate triangles/transforms, approximate Quake UVs,
triangle-soup surface count, paths, output format, and recovery limitations.
Fallback faces include invisible brush sides that have no rendered triangle;
their count alone does not imply a visible defect. Model instances and editor
grouping cannot be uniquely determined from data the BSP no longer contains.
Optional grouping proposals identify their evidence explicitly; model-instance
and source-assisted matching remain future work.

## Acceptance fixtures

- Sealed room with rotated/scaled textures.
- Brush entity with a nonzero origin and preserved key/value pairs.
- Curved patch with valid control-point dimensions.
- Large triangle overlapping a small brush face.
- Degenerate triangles and finite fallback texture axes.
- Truncated header/lump, overflowing ranges, invalid plane/vertex/shader indices,
  and malformed entity model references.

Round-trip checks compare recoverable geometry, entity data, finite texture
coordinates, and successful recompilation. Exact byte equality is inappropriate
when compiler-generated ordering or metadata changes.

`recovery_classification` adds 24 generated Quake III round trips: structural
pillars, detail pillars, a mixed `func_group`, and translucent brushes with an
explicit structural shader overriding authored detail bits. Each runs fast/full
recovery through classic, brush-primitive and Valve 220 writers. Tests check all
15 labelled world brushes, exact brush planes/materials/contents in every model,
sealing, entity links and 594 spatial samples (352,836 ordered pairs) against the
source. Fast rebuilds must match their full-recovery counterpart's node/leaf
counts, sampled cluster partition and PVS relationships. Missing VIS input and
1/4-worker byte parity are also covered.

Source-versus-rebuild partition/PVS differences are recorded, not hidden behind
successful recompilation. In the current opaque structural fixture, full and fast
recovery both change 71 nodes to 73 and add/remove 32/8 sampled visibility pairs;
brush geometry and sampled opaque space still match. These are fidelity
differences, not evidence by themselves of incorrect rendering. The rebuild-order
option now removes the observed differences in the opaque structural, ordinary
detail and mixed-group controls by compensating for loader insertion order.
The mixed group is flattened by compilation; this fixture does not implement group
inference. Finite samples and this small axial-brush corpus do not prove general
visibility or classifier correctness. See [the fast-recovery audit](validation/fast-detail-recovery.json).

`recovery_order` adds 38 rebuilds. Five positive source cases cover detail,
structural, mixed-group, translucent structural and mixed-opacity geometry,
including multiple brushes in an entity with a nonzero origin. All three formats
in fast/full mode retain per-model compiled brush order, geometry/materials/contents,
node counts, surviving entities and sampled partition/PVS relationships. Two
additional builds cover a redundant translucent side that is not exported.
Default/legacy output equivalence, 1/4-worker parity and 12 invalid option/profile
combinations are checked. A sixth source case deliberately retains contradictory
detail/structural flags: its 69 source nodes rebuild as 74 with 4,494 sampled
partition-pair differences and no sampled PVS differences. An independent source
control with those conflicting bits removed matches the rebuilt output. This
separates discarded authoring flags from ordering effects. See the bounded
[rebuild-order validation](validation/recovery-brush-order.json).

## Implemented validation

The loader checks headers and all loaded lump ranges before access, then verifies
cross-lump references, strings, finite geometry, patch dimensions, node graphs,
visibility dimensions, and Raven lightgrid indirection. Entity model references
are checked before conversion. `-force` cannot bypass these safety checks. Node
depth is capped at 1024 to protect legacy recursive traversals. A valid unaligned
lump is copied safely rather than rejected solely for its alignment.

Native Raven maps can carry non-finite lightmap UVs in slots their surfaces do
not use, including primary slots on vertex-lit patches. The loader checks every
referencing surface, then zeros only unused non-finite pairs and emits a warning.
Non-finite coordinates used by any surface still fail, including under `-force`.
Zero-geometry flares referring to fog 0 with an empty fog lump are repaired to no
fog; equivalent references on actual geometry are rejected. Recovery JSON records
these changes as `normalized_unused_lightmap_uv_pairs` and
`normalized_unused_flare_fogs`. Finite texture coordinates and active lightmap
coordinates are preserved.

## Native Alice and F.A.K.K.2 recovery

```sh
q3mapx -game alice -fs_basepath /path/to/Alice -decompile -o recovered.map input.bsp
q3mapx -game fakk2 -fs_basepath /path/to/FAKK2 -decompile -o recovered.map input.bsp
```

The asset roots contain `base` and `fakk`, respectively. Both CLI and workbench
offer recovery and minimaps for these native formats. BSP writing is disabled:
recover a standard MAP or export OBJ/ASE geometry instead of rewriting a native
file through an incompatible writer. Recovering into a different game's editor
still requires that game's entities/materials to be adapted by the user.

The mandatory MAP report includes `native_losses`, `native_shaders` (raw contents,
surface flags and shader subdivisions), and `native_surface_subdivisions` in
source surface order. These retain metadata outside standard MAP syntax. The
report explicitly identifies omitted native baked-light extensions and the
general limitations of source-light reconstruction. Native writing and lossless
round-tripping are not claimed. See [coverage and evidence](GAME-COVERAGE.md).

FTX image lookup follows the existing TGA/PNG/JPEG/DDS/KTX/CRN/WebP alternatives.
Its 12-byte header and RGBA payload are checked before allocation; dimensions
must be positive and at most 8192 per axis. Malformed images produce a diagnostic
and the usual missing-image fallback. Texture dimensions matter to Valve 220
scales, so installed assets or equivalent source textures should be available.

## Native Allied Assault recovery

```sh
q3mapx -game mohaa -fs_basepath /path/to/MOHAA -decompile -o recovered.map input.bsp
q3mapx -game mohaa -fs_basepath /path/to/MOHAA -convert -format obj input.bsp
```

The asset root contains `main`. The MAP contains recoverable brushes, entities
and Bezier patches. Terrain is available in OBJ/ASE export and the mandatory
JSON report, not as MAP brushes or patches. Native triangle hole flags are
respected. Static-model placements are retained in JSON; external TIKI meshes
are not imported. Native BSP writing and the brush-only minimap are disabled.

Additional report fields are `native_terrain`, `native_terrain_triangles`,
`native_terrain_removed_triangles`, `native_static_models`,
`native_side_equations`, `native_side_equation_indices`, and `fence_mask` in each
native shader. Terrain records contain world origin, height steps (two units
per step), texture corners, variance flags, shader index and lightmap metadata.
`native_surface_subdivisions` remains in original source surface order, excluding
the generated terrain meshes. Packed lighting and static-model color omissions
are named in `native_losses`.

`normalized_unused_native_equations` counts the narrow repair for zero equation
references in maps with neither fence contents nor an equation table. Invalid
active references are rejected. See [coverage and evidence](GAME-COVERAGE.md).

## Early Quake III recovery

Use `-game q3-ihv` for IBSP 43, `-game q3test44` for public Q3Test 1.02–1.05, or
`-game q3test45` for 1.06–1.08. `-inspect` identifies the version without loading
assets. IHV's root contains `baseq3`; public Q3Test's contains `demoq3`. These
profiles support recovery and brush minimaps, with native writing disabled.

Versions 43/44 do not contain brush-side material names. Standard recovery
infers visible-face names and texture axes from overlapping rendered triangles
within each model. Unmatched faces use `common/caulk`. `-fast` skips triangle
matching, so it cannot infer those names. Native 45 retains its shader records.
The native shader dialect is partially supported; missing textures and unusual
directives can still require manual correction.

Reports retain `native_models` (origin, head node, declared surface range),
`native_brush_contents` and `native_side_flags` in source order. For 43/44,
`native_surface_source_indices` and `native_brush_source_indices` map recovered
geometry indices back to the input order. Submodel ownership is derived from
validated trees because some native declared surface ranges are stale.
`inferred_material_faces` counts faces where spatial matching supplies a name;
`matched_uv_faces` independently counts usable UV reconstruction. Native fog
visible sides are shader dependent and are not reconstructed. See the
[format-specific evidence and limits](GAME-COVERAGE.md).

## OBJ and ASE mesh export

```sh
q3mapx -game quake3 -convert -format obj -patchsteps 8 input.bsp
q3mapx -game mohaa -convert -format ase -patchsteps 8 input.bsp
```

The workbench offers **Export OBJ mesh** and **Export ASE mesh**, with **Mesh
curve detail** under Quality & compute. Both exports include planar geometry,
triangle meshes, native MOHAA terrain, and tessellated quadratic Bezier patches.
Brush-entity origins are applied to vertex positions. OBJ uses `(x,z,-y)` axes;
ASE keeps game axes and writes world-space vertices with identity node transforms.
External model assets, entity animation and a game's runtime shader effects are
not reconstructed. Material files reference assets; they do not extract textures
from installed archives.

`-patchsteps` sets samples per quadratic span, from 1 to 32 (default 8). More
samples make smoother, larger meshes. This is a fixed-resolution export, not an
engine's distance-dependent LOD. Positions and texture/lightmap coordinates are
evaluated on the curve; normals use its tangents, with finite fallbacks for
degenerate spans. The compiler's persistent worker pool processes independent
rows into fixed ranges, preserving the same geometry and bytes across worker
counts. MAP recovery keeps the original control points.

Before allocation or opening outputs, expanded storage is capped at 4,194,304
vertices and 25,165,824 indices. Lower curve detail for a map that exceeds this
budget. Invalid options, unsafe expansion and non-finite entity origins fail
before existing exports are replaced. In development after 0.3.0, mesh streams
write to reserved sibling files. Every write/close is checked before publication;
a failed generation or close preserves the previous exports. Directories and
symbolic links are rejected as destinations. ASE publishes with one atomic
replacement. OBJ publishes its completed MTL and then its mesh; a reported later
replacement failure restores the old MTL (or removes the newly created companion).
Failures are visible to the CLI and workbench, and a retry can succeed once an
external file-sharing lock is released.

The two OBJ filenames are not a single crash-atomic transaction: process or power
loss between their replacements can leave mixed generations. Fatal exits during
generation can leave identifiable `.q3mapx-*.tmp` sibling files. If rollback itself
fails, the diagnostic names the retained original backup for manual recovery.
Avoid concurrent writers or external changes to the same output paths. Filesystem
metadata and power-loss durability are not guaranteed by this mechanism. The
packaged 0.3.0 release predates these changes.

OBJ references its actual sibling MTL filename. `-lightmapsastexcoord` and
`-deluxemapsastexcoord` retain their existing meaning; only referenced OBJ
lightmap materials are emitted, so a sparse high index does not allocate or
write every intervening material. External-lightmap shader lookup checks short
names before suffix access and uses bounded token storage. Baked lightmap image
files must be supplied separately for material previews.

[Geometry validation](validation/mesh-export-win-x64.json) includes an analytic
curve center, UVs, triangle winding, surface-local normals, brush-entity bounds,
one/four-worker byte parity and output preservation. Real OBJ probes include
IHV `ihv_test1`/`km_portal` and MOHAA `mohdm3`/`m1l1`. These exports include
curves omitted by the inherited exporters.

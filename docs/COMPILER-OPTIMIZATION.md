# Intelligent visibility and geometry optimization

## Status and scope

These are two planned compiler options, separate from decompiler inference.
They are not implemented by the existing `-vis -reproducible`, `-merge`,
`-mergeportals`, `-hint` or meta-surface processing options. Audit and measure those
existing paths as baselines before adding new behavior. CLI and workbench will use
the same analysis/results, with explicit enablement, deterministic decisions,
regional exclusions, bounded effort and a report explaining accepted/rejected edits.

The [VIS foundation audit](VIS.md) now repairs directional flags, merge bounds and
convexity, and packs live working bits without changing deterministic job order.
Matched poor-detail/manual-detail workloads compare default, merge, mergeportals
and hint modes. These are safer and more compact existing solver paths; they do
not yet select regional transformations or enforce a runtime visibility budget.

Passage construction now skips empty flood intersections, iterates only surviving
candidates and stores compact word spans in per-portal blocks. This reduces work
and retained storage with the same graph and clipping results. An isolation probe
still finds baseline-inclusion differences with leaf merging alone; omitting the
polygon merge is insufficient to validate automatic topology changes.

The [BSP evidence command](BSP-EVIDENCE.md) reports regional subdivision and
reference costs, local brush-plane associations and stored PVS statistics.
With an explicit matching PRT, [portal diagnostics](PORTAL-ANALYSIS.md) now add
regional passage-pair bounds, shape/protection/bridge observations, structural
brush samples and deduplicated stored-PVS world triangle costs. Matched known-source
detail controls and independent graph/geometry/PVS oracles validate these reports.
They do not yet identify proven-safe transformations or implement either planned
automatic compiler option.

Optional [`-cell-adjacency`](CELL-ADJACENCY.md) now derives bounded convex path
cells and coplanar interfaces directly from the BSP. This gives the correspondence
work actual polygons rather than stored boxes or center probes. It retains
enclosure and numerical uncertainty and does not recover original PRT flags.
Matched oblique portals show measurable construction differences; complete area
coverage and protected-flag correspondence remain prerequisites. After the full-
winding clipping repair, combined merging still omits 2 baseline bits on grid=5
and 27 on grid=9. No automatic transformation is enabled by this evidence stage.

Start with reports and proposals. Once a transformation meets its correctness
gates, the selected optimization mode may apply validated changes automatically
to the build copy and fall back on the unchanged region when it cannot validate
them. The user's source MAP remains intact; an edited MAP is a separate export.

## Intelligent VIS and portal generation

### Stage boundary

`bsp.cpp` constructs structural trees, creates tree portals and writes the PRT
before `vis.cpp` loads it. A VIS-only operation can simplify the graph used by its
solver, but cannot retroactively change source detail flags or undo all earlier
BSP splits. Provide two coordinated scopes rather than hiding a BSP rewrite
inside an ordinary VIS run:

- A VIS-only pass for validated portal/leaf aggregation using the existing BSP/PRT,
  preserving the mapping back to original runtime cluster rows.
- A full-build option that analyzes source geometry, changes justified structural
  participation in an internal build copy, and rebuilds BSP, PRT, VIS and affected
  downstream data together. Refuse this scope when its source/profile requirements
  are unavailable; report what the VIS-only scope can still do.

### Regional analysis and transformations

1. Attribute expensive regions to source brushes and planes: tiny/sliver portals,
   repeated coplanar cuts, excessive local splits, portal degree, cluster counts,
   estimated traversal work and weak occlusion benefit. Include examples of dense
   trim, stair assemblies and small supports compiled entirely as structural.
   Portal count alone is not a useful quality metric.
2. Compare geometric adjacency, separating planes, room/corridor structure and
   visibility behavior. Rank changes by predicted VIS time/memory reduction and
   runtime visibility cost. Account for the work saved and any extra surfaces or
   triangles made potentially visible; avoid collapsing a map into one huge PVS.
3. Extend proven graph merges only when their geometric/visibility contract holds.
   Map results back to all represented clusters conservatively; merged visibility
   may include more than the baseline, but must not lose a possibly visible region.
   Identical or similar PVS rows alone do not justify an arbitrary topology merge.
4. For full builds, consider connected decorative assemblies for automatic detail
   treatment, redundant splitter removal, local splitter selection and suggested
   hint partitions. Examine the whole region and its exterior connectivity before
   applying edits. Protect sealing brushes, major occluders, area portals/doors,
   explicit structural and hint constraints, contents boundaries and entity/model
   ownership. Retain draw and collision geometry and respect user exclusions.
5. Trial-build candidates with bounded iterations. Recheck leaks, connectivity,
   cluster/area references, conservative visibility and runtime cost after each
   accepted batch. Discard failing changes and report their reason. Newly generated
   partitions must not reuse a stale PRT, VIS, lighting cache or dependent data.

### Acceptance

Use matched source fixtures with intentionally poor detail usage and a manually
well-classified counterpart, plus open terrain, long corridors, nested rooms,
narrow openings, sky, areaportal doors and thin structural seals. Measure complete
BSP/VIS/LIGHT time, peak memory, portals/clusters, VIS bytes and visible surface/
triangle distributions from a documented camera corpus. Add adversarial cases
where a locally minor brush is the only world seal or important occluder.

For VIS-only changes with unchanged cluster IDs, require each output PVS row to
include the baseline visible set unless a separately verified exact algorithm
establishes a tighter result. For rebuilt trees, compare visibility through spatial
correspondence, including boundary cells and door states; do not compare unmatched
cluster indices. Geometry-based correctness arguments and regression fixtures are
required: sampled rays or screenshots alone cannot prove absence of false culling.
Reject candidates exceeding the chosen runtime-visibility budget even if compilation
is faster. Record improvements against both legacy/default and existing merge modes.

Run CPU reference checks and deterministic worker-count tests first. Use the
persistent job pool for independent regional scoring; evaluate GPU workloads only
where measured batches justify transfer/setup costs. Publish explicit unsupported
cases instead of enabling a speculative transformation for every game profile.

## Intelligent triangle-count optimization

The [exact planar reduction core](PLANAR-REDUCTION.md) now removes compatible
interior fan vertices with bounded effort, stable edit history and independent
rational checks. The [post-LIGHT native optimizer](GEOMETRY-OPTIMIZATION.md) now
adds bounded shader analysis, index-only IBSP46 publication and CLI/workbench
workflows for an explicit Quake3e OpenGL profile. Eligible opaque horizontal
world surfaces must already disable marks/dynamic lights and have constant native
colors/normals and exact affine UVs. The corrected generated grid reaches 512 →
144, while the adapter protects the entire point-lit grid. A failed broader
dynamic-light render matrix narrowed eligibility without relaxing the preset
tolerance. Other orientations, materials, renderers, formats and regional review
remain open; exact affine BSP attributes alone do not certify runtime behavior.

### Preservation contract

Reduce rendered triangles without changing visual presentation. Keep this separate
from draw-call reduction, index/cache reordering and approximate LOD; report each
metric independently. An optimizer that only reorders indices has not reduced
triangle count. Approximate decimation cannot satisfy the default preservation
contract just because a few screenshots look similar.

Preserve surface coverage and silhouettes, material/fog/contents assignments,
texture and lightmap mappings, vertex colors and light styles, normal/tangent
behavior, seams, winding/culling, entity ownership and patch LOD boundaries.
Keep collision and portal semantics intact; do not reuse render simplification
as collision simplification. Account for shader effects tied to vertices or primitive
topology, including deformation, autosprites, alpha/blending order and offsets.
Skip unsupported material behavior unless equivalence can be demonstrated.

### Candidate passes

1. Report triangles by region, surface and material, their source provenance,
   degenerate/redundant candidates and protected seams. Distinguish authored
   detail from compiler-generated fragmentation and inherited meta processing.
2. Remove only provably redundant geometry: zero-coverage degenerates, duplicate
   opaque triangles with identical semantics, and vertices/edges whose removal
   preserves interpolation. Overlapping translucent layers are not duplicates.
3. Retriangulate compatible coplanar regions across unnecessary splits. Require
   matching affine attribute fields over the entire region, not just equal endpoint
   UVs; preserve intentional discontinuities and lightmap/chart boundaries. Do not
   reintroduce T-junction cracks to save triangles.
4. Consider redundant tessellation of genuinely planar/attribute-compatible patch
   spans, preserving shared-edge stitching and profile-specific runtime LOD. Leave
   curved silhouettes or deformed surfaces alone when fewer triangles cannot meet
   the preservation contract. Surface representation changes need their own tests.
5. Compare a BSP-stage pass before lightmap allocation with post-LIGHT application:
   the measured point-light control demonstrates that pre-bake affine fields do
   not establish equivalent final vertex lighting. A later BSP-file
   optimizer must also preserve baked lightmap interpolation and every cross-lump
   reference, obey native writer capabilities, and pass a separate validation gate.
   Do not expose a rewrite mode for recovery-only profiles.

### Acceptance

Use over-tessellated planes, collinear edges and duplicates alongside adversarial
UV shear/seams, non-affine light/color interpolation, hard normals, normal mapping,
fog boundaries, alpha layers, vertex-deformed shaders, curved patches, LOD joins,
submodels, thin silhouettes and T-junctions. Require topology/attribute checks,
collision and visibility parity, and deterministic results across worker counts.

Compare before/after render targets under fixed cameras and moving-view sequences,
including multiple distances, texture filtering/mip levels, shader animation times
and supported lighting/material modes. Any game validation must run windowed and
use the engine's registered screenshot/render-target capture path, without input
injection or OS capture. Pixel comparisons supplement the equivalence checks; a
finite camera corpus is not proof for all views. Reject or leave unchanged cases
where preservation cannot be established. Publish removed triangles, memory, build
time and measured renderer cost, including maps where no safe reduction is possible.

The geometric error alone is insufficient: the authors' description of
[appearance-preserving simplification](https://userpages.cs.umbc.edu/olano/papers/index.html)
also highlights shading differences after small geometric changes. That motivates
the attribute/material checks here; it is not a claim that that research method
meets q3mapx's strict preservation contract or is being imported.

## Delivery sequence

The starting code audit at `afe45c8` covers structural tree/PRT generation in
[bsp.cpp](../tools/quake3/q3map2/bsp.cpp), geometric portal construction in
[portals.cpp](../tools/quake3/q3map2/portals.cpp), existing merge modes and PVS
publication in [vis.cpp](../tools/quake3/q3map2/vis.cpp), and existing vertex/triangle
deduplication and meta-surface processing in
[surface_meta.cpp](../tools/quake3/q3map2/surface_meta.cpp).

1. Add shared region/provenance analysis, comparison reports and known-source
   adversarial fixtures. Establish correctness and performance baselines for the
   current compiler, including existing merge/meta options.
2. Deliver VIS inefficiency diagnostics and triangle opportunity reports, with
   workbench region selection and a CLI-only workflow.
3. Implement conservative VIS graph changes and strictly equivalent triangle
   reductions as separate task commits, with independently selectable options.
4. Develop full-build structural/detail optimization and more complex surface
   reductions only after their earlier validation gates pass. Keep decompiler
   authoring inference independently configurable throughout.

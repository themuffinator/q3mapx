# Recovery inference and authoring fidelity

## Status and objective

This roadmap was added on 2026-09-30. The first shared
[BSP evidence command](BSP-EVIDENCE.md) and optional
[`-detail-policy cells` exporter](DECOMPILATION.md#detail-inference-policy) are
implemented with bounded geometry and per-brush decisions. The broader group,
light and review tools below remain planned. Recover the closest supported
recreation of the author's MAP: editable geometry, organization, materials,
visibility behavior and source lighting. Keep exact extraction available alongside
optional inference, and preserve the input BSP and existing recovered output.

Compilation removes information. Several source maps, brush classifications,
groupings and light arrangements can produce equivalent compiled results. Reports
must distinguish preserved facts, reconstructed geometry, inferred choices and
user corrections. An inferred grouping or lighting solution must never be labelled
the original author's recovered metadata solely because its rebuild looks similar.

## Shared evidence and review

- Build a reusable, bounded analysis model from validated planes, nodes, leaves,
  brushes, surfaces, clusters/PVS, entities, materials and native extensions.
  Record the BSP hash, game profile, asset identities and assumed compile settings.
- Associate each proposal with source record indices, spatial bounds, supporting
  and conflicting evidence, alternatives, and a confidence category. Calibrate
  confidence against known-source fixtures before publishing numeric probabilities.
- Use a versioned sidecar for proposals, source-to-output correspondence and saved
  overrides. Preserve schema compatibility for existing consumers; define and test
  the report extension before adding required fields or changing their meaning.
- Provide CLI analysis, selectable inference stages and deterministic export. Add
  workbench overlays for detail/structure, proposed groups, cluster boundaries,
  recovered lights and lighting residuals. Users can accept, reject, split, merge
  or lock proposals and rerun affected regions without losing manual corrections.
- Run comparisons and rebuilds in independent project output directories. Keep
  the extracted baseline and each proposed reconstruction available for comparison.

## Detail and structural geometry inference

The default converter's `detailBrushes` table marks brushes referenced by leaves
with nonopaque clusters, then excludes explicit structural shader flags. This is
a useful starting heuristic, not a complete reconstruction of portal participation.
Audit it against known detail/structural labels before replacing or extending it.
The first expanded [round-trip corpus](DECOMPILATION.md#acceptance-fixtures) now
covers 24 fast/full recoveries of structural/detail pillars, a mixed group and
explicit structural overrides. That audit found and repaired dropped detail
flags in fast export. It also records remaining source/rebuild partition and PVS
differences despite exact brush geometry, plus the ambiguity of brush-model
detail flags. These controls precede any replacement classifier; they do not
establish accuracy on arbitrary BSPs or recover original group membership.

The explicit [`-brush-order rebuild` option](DECOMPILATION.md#brush-order-for-rebuilding)
now removes the ordinary fixture partition/PVS differences caused by opaque
brush insertion reversing stored order. A second corpus checks 38 rebuilds,
including mixed opacity and discarded redundant sides. Contradictory numeric
detail flags on structural source sides remain a distinct limitation: the
compiler retains their splitter-priority effects without preserving those flags
as recoverable source metadata. Known-source controls isolate this effect rather
than interpreting every partition difference as a classification error.

The first optional classifier now clips brush interiors through the world tree,
without relying on leaf-brush references. Strict interior witnesses and current
exported-side material semantics support detail proposals; protected or uncertain
cases retain the legacy baseline. A 36-rebuild corpus includes deliberately
removed references and non-first-side structural materials, alongside analytical
geometry, VIS, budget and worker controls. It does not yet reconstruct complete
leaf adjacency, accept PRTs, prove portal causality, save user overrides or expose
GUI proposal review. Broader real-map classification calibration remains open.

1. Relate brush faces to BSP partition planes, adjacent leaf cells, opaque space,
   cluster boundaries and PVS changes. When a compatible PRT is supplied, use its
   portal graph directly. Otherwise investigate reconstructing bounded leaf-cell
   adjacency from the BSP tree; do not assume the original PRT is stored in the BSP.
2. Estimate which brushes supported visibility partitions and which were inserted
   without creating them. Separate retained compiler flags from geometric evidence.
   Identical PVS rows, brush size or a leaf reference alone cannot establish the
   author's detail choice. Missing, fast, all-visible or unreliable VIS lowers
   confidence and must not turn an unknown classification into a claimed fact.
3. Protect sealing structure, major occluders, area portals, hint/skip behavior,
   collision and contents boundaries, sky and water boundaries, and brush-model
   ownership. Detail recovery must retain collision geometry and materials.
4. Test candidate classifications through controlled BSP/VIS recompilation.
   Compare sealing, spatially corresponding leaf/cluster membership, portals and
   conservative visibility. Cluster IDs can change, so raw PVS byte equality is
   insufficient for a rebuild with a different tree.
5. Export supported detail flags only when evidence meets the selected recovery
   policy. Keep uncertain candidates visible in the report with an explicit
   override; retain the conservative baseline when validation fails.

Acceptance fixtures include structural pillars and otherwise identical detail
pillars, seals resembling trim, mixed detail/structural groups, sloped brushes,
hint planes, doors/area portals, nested rooms, leaks and absent or fast VIS.
Measure classification precision/recall against source labels and the cost of
wrongly classifying structural brushes; successful recompilation alone is not
evidence of correct classification.

## func_group inference

Propose useful authoring groups from connected geometry, shared transforms and
grid patterns, material/UV continuity, repeated assemblies, region boundaries and
compatible compile properties. Group identity is a separate decision from detail
classification: a group may contain both structural and detail brushes.

The current MAP loader applies group compile parameters before collapsing
`func_group` brushes into worldspawn. Preserve evidence of shadow, ambient,
lightmap scale/sample and related properties; do not invent values for lost keys.
Grouping for editor convenience must not silently change those effective settings.
Never transfer geometry between distinct brush entities/models or absorb doors,
triggers, area portals or origin brushes into a proposed world group.

Emit deterministic inferred group names, membership/provenance and alternative
partitions. Preserve any surviving identifiers and user-assigned names. Evaluate
against known-source assemblies, including plausible but deliberately ambiguous
groupings; report grouping similarity separately from rendering/compile parity.

## Light entity and spotlight inference

Target BSPs whose static light entities were stripped, while retaining and locking
lights or target links that survive. Fit a plausible source-light arrangement to
baked observations using the selected game's lighting semantics.

1. Extract world-space lighting samples from lightmaps, vertex lighting and the
   lightgrid; use directional/deluxe channels and separate styles when available.
   Track face normals, UV charts, sample support and invalid/padded texels. Decode
   profile-specific exposure, gamma/overbright, compression and clamping where
   known. Treat saturated samples and unknown bake settings as uncertainty.
2. Build a forward explanation from surviving lights, shader surface emitters,
   sky/sun/environment lighting, ambient/minlight, bounce and other supported bake
   effects. Missing shader assets must be reported. Avoid fitting a field of point
   lights to compensate for unmodelled sky illumination, emissive surfaces or bounce.
   Estimate uncertain global parameters jointly where supported instead of simply
   subtracting guessed byte values from an encoded lightmap.
3. Generate candidate point and spot lights from unexplained spatial illumination,
   normal/direction evidence, shadow boundaries and plausible fixture locations.
   Score them using the compiler's own attenuation, occlusion, alpha/filter and
   material behavior. Begin with deterministic CPU reference evaluation and reuse
   bounded batches of transfer responses where geometry/material state permits.
4. Fit nonnegative colors/intensities and refine position, falloff, spot direction
   and cone parameters. Prefer a small explanatory set with a complexity penalty;
   remove duplicate/unsupported candidates and validate on held-out samples.
   Handle a game's dark/subtractive light flags as separate supported source types,
   rather than using arbitrary negative intensities to conceal a poor fit; leave
   unsupported effects in the residual with a diagnostic.
   Increase search effort by explicit quality budget, with cancellation and saved
   intermediate proposals. Benchmark CPU jobs and optional GPU batches including
   setup costs before selecting automatic acceleration.
5. For an inferred spotlight, search surviving target entities and targetnames for
   a geometrically and semantically compatible aim point. Account for the game's
   radius-versus-target-distance convention. Reuse an existing link only when it
   fits and does not alter gameplay relationships. If the required target was
   stripped, propose a dedicated, uniquely named, game-supported target entity and
   connect the inferred light to it; label both as inferred. Never silently retarget
   unrelated entities. Report ambiguous point-versus-spot alternatives.
6. Rebuild the candidate lighting and compare at corresponding world samples,
   independently of new atlas packing. Report linear-light residuals, directional
   error, shadow-edge mismatch and held-out error as well as hypothesis confidence.
   Preserve unexplained residuals and alternative solutions; similarity of baked
   lighting does not prove the original light count, positions or target names.

Acceptance uses known-source bakes with the light entities then removed from the
recovery input: single/multiple/colored point lights, narrow/wide overlapping spots,
retained and stripped targets, sun-only, emissive-only, mixed sky/surface/entity
lighting, bounced rooms, filtered/alpha shadows, light styles, clipped lightmaps
and missing assets. The hidden original MAP supplies evaluation labels only, not
candidate generation. Sun-only and surface-only cases specifically test false
entity-light inference. Publish localization/count metrics on identifiable cases
separately from re-bake fidelity on ambiguous cases. Set numerical release gates
from this corpus before enabling inferred-light export by default.

## Additional accuracy tools

- Recover brush-face UVs from multiple consistent triangles using robust fitting;
  detect genuine seams and mixed mappings rather than averaging them away.
- Reassemble compatible compiler-split brush fragments while preserving the solid
  union, contents, plane orientation, materials and per-face UV functions. Infer
  likely grid/plane snapping only when an error bound and rebuild check justify it.
- Preserve source patch control points where present. Investigate patch/curve or
  repeated model-instance reconstruction from tessellated meshes as separately
  labelled proposals, with silhouette, surface, UV and normal error bounds.
- Improve entity-link validation, model/origin placement, game-specific key handling,
  caulk/hidden-face recovery and native metadata export. Use matching source assets
  as evidence for model instances; do not guess an asset name solely from proximity.
- Add a rebuild comparison tool for solid/collision coverage, visible surfaces,
  materials/UVs, patch shapes, entity relationships, visibility and lighting.
  Show localized difference overlays and explain which discrepancy an inference
  resolves or introduces. This is also the check for saved manual corrections.
- Offer source-assisted matching when the user supplies partial MAP fragments or
  prior revisions, clearly recording which data came from that additional source.
  Keep source-free inference evaluation separate to avoid overstating recovery.

## Delivery order and research basis

Implementation starting points audited at `afe45c8`:

| Area | Existing source |
| --- | --- |
| Geometry/UV recovery and current detail heuristic | [convert_map.cpp](../tools/quake3/q3map2/convert_map.cpp) |
| Group compile parameters and collapse into worldspawn | [map.cpp](../tools/quake3/q3map2/map.cpp) |
| Point/spot/sun interpretation, target lookup and attenuation | [light.cpp](../tools/quake3/q3map2/light.cpp) |
| Tree portals, flood/area connectivity and VIS input/output | [portals.cpp](../tools/quake3/q3map2/portals.cpp), [vis.cpp](../tools/quake3/q3map2/vis.cpp) |

First deliver the evidence schema, known-source corpus and comparison tools;
then detail classification and group proposals; then point-light fitting with
surface/sky explanations, followed by spotlight/target recovery. Broader geometric
reconstruction follows the same evidence and rebuild checks. Ship each as a
separately tested and documented task; inference options remain explicit until
their accuracy and supported profiles are established.

The fitting approach is a project design proposal. The official
[Mitsuba inverse-rendering tutorial](https://mitsuba2.readthedocs.io/en/latest/src/inverse_rendering/advanced.html)
illustrates that matching a rendered observation can still leave an ambiguous
recovered light environment and motivates regularization. This is conceptual
background, not a dependency choice or a claim that a physical renderer reproduces
q3map2's lighting. Implement and verify the existing game-specific forward model
first; audit licenses and credit sources before incorporating any external code.

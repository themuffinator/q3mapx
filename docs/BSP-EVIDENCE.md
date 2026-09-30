# BSP geometry and visibility evidence

Development builds after 0.3.0 include a read-only analysis command:

```sh
q3mapx -game quake3 -bsp-evidence example.bsp
q3mapx -game ja -bsp-evidence -report evidence.json -region-depth 3 example.bsp
```

The default output is `example.evidence.json`. Select the appropriate game
profile, using `-inspect` to identify candidates if necessary. Unlike the bounded
directory inspector, this command uses the full native reader and validates
geometry, graph references and entities. It needs no shader, texture or model
assets. The normal command initializes filesystem search paths, but the analysis
does not load those assets. All six recovery-only profiles are accepted.

This is the first shared evidence layer for [recovery inference](RECOVERY-INFERENCE.md)
and [compiler optimization](COMPILER-OPTIMIZATION.md). It reports observations;
it does not infer detail flags, groups or lights, classify excessive portalling,
reconstruct portals, or change compiled geometry/visibility. Workbench overlays
and automatic compiler transformations remain planned.

## What the report contains

JSON schema version 1 has `report_kind: "bsp_evidence"`. It is a separate report
from the directory inspector and MAP recovery reports; their schemas are unchanged.

| Field | Meaning |
| --- | --- |
| `source` | Absolute source path, SHA-256, byte count, selected canonical profile, q3mapx version and scope of validation/assets/settings |
| `counts` | Validated normalized records, entities, recovered terrain, unloaded static-model instances and recorded compatibility normalizations |
| `world_graph` | Native world head (negative means `-leaf-1`), reachable node/leaf records and whether unique node paths permit local analysis |
| `models` | Normalized brush/surface spans and stored bounds |
| `brushes` | Ownership, contents, native brush provenance, side indices, partition associations, leaf-reference counts and optional axial-plane enclosure |
| `visibility` | Presence, stored dimensions, number of referenced cluster IDs, visible-pair counts, row extrema, density, all-visible status and missing diagonal bits |
| `regions` | Node subtree summaries ranked by descending subdivision count, with depths, bounds, brush/surface references, indexed triangles and patch references |
| `limits` | Record, expanded-side, work and output ceilings, plus consumed work units |
| `limitations`, `native_recovery_losses` | Evidence boundaries and unavailable format-specific content |

Brush `model_ownership` is `unique`, `unowned` or `overlapping`. `model` is null
for the latter two cases. Model spans are examined with difference arrays instead
of expanding overlapping ranges. Early BSP brush records may be reordered by the
native reader: `index` refers to normalized order, while `source_brush_index`
identifies the original disk record. Other side/node indices refer to normalized
arrays. Native terrain contributes recovered indexed triangles; external static
model meshes are not loaded or counted as triangles.

`stored_detail_content_flag` and `stored_structural_content_flag` only decode the
selected profile's retained content masks. A null value means that profile has
no such mask. These flags do not establish the author's source classification.
`nonopaque_leaf_references` counts references from cluster IDs at least zero;
`world_leaf_references` restricts references to leaves reachable from the world
head. Counts include repeated references. They are not unique brush counts.

`world_partition_side_indices` lists brush sides whose plane coefficients match
a reachable world node. Matching ignores orientation and signed zero, including
duplicate reversed plane records, without assuming that neighboring disk planes
form correct pairs. Nearby or scaled-equivalent coefficients are not merged.
`leaf_path_partition_side_indices` further requires a matching node on a path to
a world leaf referencing that brush. This excludes a matching cut confined to an
unrelated region. It still does not prove that the brush caused the split.

Local traversal is iterative, with active ancestor-plane counts. A shared acyclic
node graph disables local side observations (null) and subtree summaries (empty)
instead of expanding every possible path. A missing world head also disables
local observations. Duplicate leaf references are permitted and counted by path
in region totals. A one-leaf world has no partition sides or node regions.

`axial_plane_enclosure` is a conservative box formed only from retained axial
brush planes, in model-local coordinates. It is null when those planes cannot
provide all six finite bounds or contradict one another. Other planes can tighten
the actual brush; the box is not an exact clipped solid or a validity proof.

PVS counts mask unused bits in the final meaningful byte and ignore row padding.
Missing VIS produces null statistics, never a synthesized all-visible table.
An empty table has no density/all-visible result. Fast VIS cannot be identified
reliably from the stored bytes, so `compile_mode` remains `unknown`. A missing
self bit is exposed as an observation, without silently modifying the table.

## Regional investigation

`-region-depth N` selects node subtrees at depth N, or terminal nodes above that
depth (default 4, range 0–8). The selected subtrees do not overlap. Early leaf
children outside those subtrees are omitted, so the list is not a complete
partition of all geometry. Depth zero reports the complete reachable world
subtree when it has a unique node path. At most 256 regions are produced.

Counts describe the compiled tree. Stored node bounds are not reconstructed leaf
cells. Brush/surface and triangle counts include reference multiplicity; patches
have a separate count because runtime tessellation is not present in their index
arrays. Shader passes, runtime LOD and external models also affect actual rendering
cost. A large subdivision count is a site to investigate, not a demonstrated
inefficiency or an instruction to convert geometry to detail.

## Bounds and output protection

The source must meet the native reader's existing 2 GiB load limit. Its signature
and version must match the selected profile even with `-force`. Analysis adds
limits of two million relevant normalized records, eight million expanded brush
sides and 50 million work units. `-max-work N` changes the work budget within
1–100,000,000. Units account for record visits, expanded leaf references, brush-side
path comparisons and meaningful PVS bytes; they are not elapsed time or exact CPU
instructions. These analysis limits apply after the native loader's validation,
not as a timeout on all preexisting parser/normalization work.
The subsequently repaired shared-index validator now avoids repeatedly scanning
overlapping surface ranges; see [its validation/performance contract](PERFORMANCE.md#shared-bsp-index-validation).

JSON streams through a bounded buffer with a 64 MiB ceiling. Exhaustion is an
error, not a truncated successful report. Output must end in `.json` and cannot
alias the input, including through a hard link. Checked sibling staging preserves
existing output on analysis, write, close or publication failure. Linked and
nonregular destinations are rejected by the shared output helper. Source SHA-256
is checked before loading and after analysis; keep source and destination stable
during the command. These checks do not establish a concurrent-writer transaction
or promise preservation of old file metadata.

## Validation

`tests/bsp_evidence.py` builds original, asset-independent structural/detail pairs
with 25 otherwise identical pillars. The structural map has 159 nodes and 84
clusters; the detail map has 19 nodes and 4 clusters. Local matching sides drop
from 150 to 25. Every detail pillar still matches the common floor plane: an
explicit negative example for treating any partition match as structural proof.
This measures evidence, not classifier accuracy or an optimizer's performance.

A separate spatial control checks that a coplanar cut in another subtree is
excluded from local evidence. Other cases cover exact report parity at 1/4 workers,
missing/fully visible/padded PVS, missing diagonal bits, duplicate reversed planes,
shared graphs, cycles, bad side references, overlapping model spans, native early
nonzero world roots, six recovery-only profiles, input/hard-link protection,
budget exhaustion, the actual 64 MiB streaming ceiling, and Linux buffered-write
failure. Source maps/BSPs and
preexisting output are checked for preservation. See the
[validation record](validation/bsp-evidence.json) for executed platforms and
private native probes. No proprietary map geometry is committed.

Optional native probes use the existing private-input harness:

```sh
python tests/native_maps.py --compiler build/release/bin/q3mapx --game ja \
  --bsp /path/to/map.bsp --evidence --work-dir build/native-evidence/ja
```

The full per-brush report stays in the selected output directory. The harness's
`validation.json` retains hashes, counts and graph/PVS summaries, without copied
geometry, for a shareable validation record. Whole-command probe times include
loading, hashing and report publication; they are not comparative speedup evidence.

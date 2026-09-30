# Reconstructing BSP cell adjacency

Development builds after 0.3.0 can reconstruct bounded world-tree cells directly
from a BSP, without requiring its original PRT or game assets:

```sh
q3mapx -game quake3 -bsp-evidence -cell-adjacency example.bsp
q3mapx -game ja -bsp-evidence -cell-adjacency -brush-cells -portals example.prt -report cells.json example.bsp
```

The optional `cell_adjacency` object extends the schema-1
[BSP evidence report](BSP-EVIDENCE.md). Default reports retain their existing
fields and scope. The command does not alter the BSP, PRT, MAP or stored PVS.
This supplies geometric evidence for [detail inference](RECOVERY-INFERENCE.md)
and [regional portal optimization](COMPILER-OPTIMIZATION.md); it does not change
the current decompiler policies or enable an automatic VIS transformation.

## Geometry and interpretation

The enclosure is the stored world model's bounds expanded by one unit on each
side. The analyzer normalizes node planes in double precision, clips the enclosure
through the actual world tree and retains positive-volume leaf-path cells. It
constructs each cutting face from crossed edges, avoiding an oversized base
winding. Descendant fragments retain the partition node and side that generated
them. Pairwise convex intersections of opposing fragments on that partition
produce interfaces; bounding boxes only reject separated candidates.

The algorithm uses the native world head, including early formats' nonzero or
negative roots. Repeated references to one leaf record produce distinct geometric
cells. A missing world head or shared internal node disables reconstruction;
cyclic or invalid references are rejected by validation. Stored leaf bounding
boxes and leaf-brush references are not the geometry oracle. PVS and an optional
supplied PRT do not affect the reconstructed geometry.

| Field | Meaning |
| --- | --- |
| `status` | `reconstructed` when analysis completes and the volume sum meets its tolerance; `volume_mismatch` leaves results inconclusive; `world_tree_unavailable` has no cells/interfaces |
| `enclosure`, `enclosure_volume` | Explicit artificial clipping box and volume, meaningful when the world tree is available |
| `summed_cell_volume` | Sum including open and opaque path cells, compared with the enclosure volume |
| `open_faces_on_enclosure` | Count of open cell faces touching the artificial box; exterior completeness is unknown when nonzero |
| `degenerate_fragments` | Observed positive-area slivers discarded during clipping/intersection, collapsed cap point sets and nonpositive cell volumes; these are operation events, not distinct source features |
| `cells` | Stable cell index, normalized leaf index, cluster, volume, face count, bounds and a vertex-average interior-point candidate |
| `interfaces` | Front/back cell indices, originating partition node, area, polygon vertices and interface kind |
| `limits` | Geometry capacities and numerical thresholds used by this analysis |

Front and back refer to the positive and negative side of the reported partition
node plane. `between_clusters` joins different nonnegative cluster IDs;
`within_cluster` joins separate cells with the same cluster; `open_opaque` joins
a nonnegative cluster to a negative one. Interfaces whose endpoints both have
negative clusters are omitted. These names describe the stored cluster assignment,
not material opacity, collision contents, connected gameplay areas or author intent.

Read the numerical diagnostics even when `status` is `reconstructed`. Volume
balance is a consistency check, not a topology proof. A degenerate fragment can
affect a narrow connection while contributing negligible volume. Treat its exact
topology as uncertain. The interior point is an arithmetic vertex average, not a
maximum-clearance solution or the separately validated brush-interior witness.
Open enclosure faces are not a certified leak: the stored model bounds may omit
relevant exterior space. The report makes no claim outside its enclosure.

`original_prt_recovered` and `author_classification_proven` are always false.
Hint/sky/unknown PRT flags, compiler history, tiny-portal removal and the original
author's structural participation are not recovered from this geometry alone.
Supplying `-portals` retains the existing independent portal analysis and its
limited center probes; this option does not certify that supplied file's complete
geometric correspondence. No reconstructed graph is substituted into VIS.

## Bounds and failure behavior

Reconstruction is iterative, including graph validation. It supports up to
250,000 output cells, one million retained boundary faces, four million retained
boundary-point occurrences, one million output interfaces and a separate four
million output-point occurrences. Each intermediate cell is limited to 1,024 faces
and 8,192 point occurrences; pending traversal cells together are limited to
262,144 points. The shared native evidence stage already caps relevant normalized
records at two million; the standalone core also limits input node/leaf arrays.

Output geometry stays within an enclosure whose coordinates have magnitude at
most 10,000,000. Cap vertices within `1e-7` units merge; intersection edges no
longer than that threshold are skipped. Positive faces at most `1e-10` square
units are discarded and counted. The volume check accepts an absolute difference
of at most `max(1e-6, enclosure_volume * 1e-8)`. These are numerical tolerances,
not a promise of exact small-feature preservation.

All evidence stages share `-max-work`, defaulting to 50 million units, with the
existing allowed range 1–100,000,000. The counter includes node/leaf validation,
point classifications, cap deduplication and face-pair/intersection work; it is
not a wall-clock limit or an exact instruction count. High-degree partitions can
require many opposing face comparisons and exhaust this budget. The implementation
is currently serial; worker-count parity does not imply parallel acceleration.

An invalid enclosure or an exhausted work/geometry budget fails without publishing
a partial report. The existing checked writer caps the entire report at 64 MiB
and preserves previous output on reported write/publication failures. Input hashes
are checked before and after analysis. These guarantees retain the existing
restrictions on concurrent source/output changes and filesystem metadata.

## Validation and remaining gates

`cell_graph` compares generated box-tree adjacency and areas with an independent
box-face oracle. Its 4,772 adjacency/area and rejection checks are complemented
by analytic oblique cuts, scaled planes, single/shared leaves, resolvable thin
slabs, cap collapse at the merge tolerance, exact capacities and a 10,000-node
coplanar tree. It rejects malformed references, cycles, shared nodes, invalid
planes/enclosures and exceeded work/memory limits.

`cell_adjacency` builds matched Quake 3/Raven structural/manual-detail maps and a
64-sided sealed corridor. Every interface vertex is checked against both native
tree paths, then every interface is matched one-to-one with the compiled PRT by
endpoint, area and bidirectional polygon containment. Both grid=3 structural
fixtures have 72 cells and 238 interfaces; detail counterparts have 20 cells and
20 interfaces. The corridor has 327 cells, four 64-point openings and 322 opaque
interfaces. This validates these known pairs, not arbitrary PRT equivalence.

The axial fixtures have identical interface areas. The oblique PRT construction
differs numerically from clipping its serialized BSP planes: the observed maximum
plane/edge deviation is about 0.0474 units, and the largest relative face-area
difference is 0.647%. Tests record these discrepancies under separate 0.1-unit
and 1% PRT-comparison tolerances, while reconstructed vertices must satisfy the
native path halfspaces within `2e-6` units. The corridor also reports 194
degeneracy events. These results prohibit presenting this option as exact recovery
of the original PRT.

Controls cover absent/present VIS, one/four workers, supplied/absent PRT, combined
brush-cell evidence, modified leaf bounds, repeated leaves, shared nodes, exact
and exhausted work budgets, invalid enclosures and protected output. Six generated
recovery-only formats reproduce the same normalized graph. Relocated early world
heads and single-leaf worlds exercise native root handling. See the
[validation record](validation/cell-adjacency.json) for executed platforms,
identities, larger controls and remaining limitations.

Next steps are bounded area-coverage comparison with supplied portals, reliable
transfer of protected flags only when correspondence is established, and regional
candidate validation. Original authoring causality, global topology completeness,
visibility inclusion and runtime-cost gates remain open. Existing merge modes
still omit baseline visibility bits on the poor-detail controls; this new report
does not make those transformations eligible for automatic use.

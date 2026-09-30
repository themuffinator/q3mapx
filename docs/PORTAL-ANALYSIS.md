# Regional portal diagnostics

Development builds after 0.3.0 can analyze a PRT alongside its compiled BSP:

```sh
q3mapx -game quake3 -bsp-evidence -portals example.prt example.bsp
q3mapx -game ja -bsp-evidence -portals example.prt -region-depth 2 -report portals.json example.bsp
```

Use the BSP and PRT from the same structural compilation. To retain a PRT while
also producing stored visibility, run VIS with `-saveprt`. Analysis works before
VIS, but leaves visibility-dependent costs null. It needs no game assets and
does not modify BSP, PRT, MAP or PVS data. `-brush-cells` can be requested together
with `-portals` under the same work budget.
Optional [`-cell-adjacency`](CELL-ADJACENCY.md) adds geometric BSP path cells and
interfaces to the same report, also without changing the supplied PRT. These
objects remain independent: complete polygon coverage/pairing certification is
not yet implemented, and original protected flags cannot be inferred from the
BSP cell geometry alone.

This is a diagnostic step toward the [intelligent VIS compiler](COMPILER-OPTIMIZATION.md),
not its automatic optimization mode. It identifies expensive regions and evidence
worth investigating. It does not establish that a cut can safely be removed or
that a brush should be made detail. Trial rebuilds, geometric correspondence,
visibility inclusion and runtime-cost gates remain required before automation.

## Reading the report

The existing schema-1 BSP evidence report gains an optional `portal_analysis`
object. Reports without `-portals` retain their existing fields and scope.

| Field | Meaning |
| --- | --- |
| `source` | Explicit PRT path, SHA-256 and byte count; `pairing_proven` is always false |
| `components`, `bridge_portals` | Connected components, including isolated clusters, and openings whose individual removal disconnects their endpoints |
| `ordered_portal_pairs_upper_bound` | Sum of `degree * (degree - 1)` over clusters, before orientation and visibility pruning |
| `portal_bitset_bytes` | One bitset over both directions of all input openings, rounded to a 64-bit word |
| `three_portal_bitsets_bytes` | Storage for three such bitsets per directed input portal |
| `passage_bitsets_bytes_upper_bound` | Unfiltered ordered portal pairs multiplied by one portal bitset's size |
| `pair_probes` | Portal-center offsets agreeing/disagreeing with the named BSP clusters, unavailable probes and unusable windings |
| `regions` | Ranked subtree/remainder costs, cluster membership, boundary/protection counts, limited brush associations, stored-PVS costs and investigation reasons |
| `cluster_costs` | Degree, component, regional assignment, reachable leaf count and deduplicated world surface/triangle counts |
| `openings` | Original zero-based portal IDs, endpoint clusters, raw flags, bridge status, shape measures and probe result |

The bitset estimates describe the input graph, before optional VIS merges. They
exclude portal objects, scratch, allocation overhead and other compiler state.
They are not measured peak memory or predictions of elapsed time. Degree work is
an upper bound on candidate passage pairs, not a count of passages actually built.

Parallel openings remain distinct edges. A second opening between the same
clusters prevents either edge alone being a bridge; it may represent another
doorway and is not automatically redundant. Hint bit 1, sky bit 2 and all other
flag bits remain visible. These boundaries and graph bridges warrant protection
and geometric review when considering later transformations.

## Regions and structural associations

The frontier is the existing BSP evidence selection: node subtrees at
`-region-depth` (0–8, default 4), plus terminal nodes above that depth. A cluster
is assigned to a subtree only if all of its reachable leaf paths belong there.
Earlier leaf children, clusters spanning subtrees and unmapped clusters go into
a separately labelled remainder. Thus each cluster contributes its degree work
exactly once. Boundary openings occur in both incident regions' counts.

Shared internal BSP nodes disable regional path attribution. Repeated leaf
references are handled separately: they may put a cluster in the remainder,
but do not duplicate its reachable leaf count or world geometry. Unmapped PRT
clusters or incompatible VIS dimensions disable stored visibility costs. A PRT
with too few clusters to cover a reachable BSP cluster is rejected.

For a bounded structural lead, each region retains at most the 64 lowest-index
world brushes referenced by its leaf paths, including opaque leaves. A retained
brush enters `associated_world_brush_sample` only when a brush-side plane also
matches a local partition node and a world leaf-path association. Matches use
exact unoriented BSP coefficients. The sample is incomplete; absence from it
does not exclude a brush. These are normalized BSP brush IDs, not original MAP
brush numbers, and coincident planes do not establish which brush caused a cut.

Regions sort by descending degree work, with stable frontier-index ties. Reasons
highlight at least eight clusters with 128 candidate pairs, at least 90% stored
internal visibility over eight clusters, repeated neighbors, or at least four
slender openings comprising a quarter of incident openings. These explicit
heuristics select review sites; they do not certify inefficiency. Fast/all-visible
VIS can trigger a weak-occlusion finding without proving poor source detail usage.

## Shape, input correspondence and runtime costs

Shape analysis uses double precision on parsed float coordinates. It rejects
repeated points within 1e-7 units, coordinates above 10,000,000 units, degenerate
area, and nonplanar/nonconvex windings outside a 0.01-unit tolerance. Collinear
distinct points are allowed. An unusable winding has null shape measures.
Opaque face geometry is not analyzed here; its syntax and indices are checked.

Small openings have area below 64 square units. Slender openings have compactness
`4*pi*area/perimeter²` below 0.1. `area_perimeter_width` is `2*area/perimeter`, a
shape measure rather than an exact minimum width. Centers are vertex averages.
The report probes 0.02 units along both directions of a usable portal normal
through the BSP tree and compares the resulting unordered cluster pair.

Agreement is only a local observation. It cannot prove that a winding fits the
full BSP cells or that the files share a compilation. Disagreement can expose
stale inputs, but thin cells and rounding also require review. The CLI warns
about unavailable world paths, unmapped clusters, disagreements and unusable
windings. These conditions must not be treated as permission to change geometry.

When compatible stored PVS exists, every row unions world-model surface IDs
from visible clusters before counting indexed triangles and patch surfaces.
Duplicated references and surfaces appearing in multiple clusters count once.
Padding and unused PVS bits are ignored. Region summaries include internal PVS
density and mean/maximum visible world indexed triangles.

These costs describe the existing BSP. Submodels, external meshes, runtime patch
tessellation, foliage instance expansion and shader passes are not included;
frustum and runtime area-mask filtering are not applied. They are not a complete
renderer benchmark or the cost of a proposed new partition.

## Bounds and validation

VIS and portal evidence share a bounded PRT1 reader. It accepts decimal/scientific
finite float coordinates, optional leading plus signs and ASCII whitespace,
including CRLF. It checks parentheses, complete records, indices, self-edges,
per-cluster winding counts and trailing data. Default limits are 16,384 clusters,
65,536 undirected portals, 262,144 faces, 512 points per winding, 1,024 incident
portals/faces per cluster (separately), eight million point occurrences and
256 MiB input. These are hard limits even with `-force`.

The combined BSP/portal analysis uses the existing `-max-work` budget (default
50 million, maximum 100 million) and streaming 64 MiB report ceiling. Budget
exhaustion publishes no partial report. Both inputs are hashed before/after
analysis; report destinations cannot replace either input, including aliases.
Concurrent external modification remains unsupported.

The generated nine-column fixture deliberately changes only source detail usage:

| Source classification | Clusters | Openings | Candidate ordered pairs |
| --- | ---: | ---: | ---: |
| Columns structural | 40 | 94 | 816 |
| Columns detail | 4 | 4 | 8 |

Both Quake 3 and Raven controls produce these graph counts. This is evidence
that the report exposes the structural difference, not a measured speedup or an
automatically accepted edit. An 81-column control on Windows gives 220 clusters,
718 openings and 13,586 candidate pairs for structural columns; the detail version
still gives 4, 4 and 8 respectively, in both profiles.

Regression oracles independently remove edges to
find bridges, traverse the region frontier, triangulate polygon fans, and union
stored-PVS world surfaces. Tests also cover stale pairs, flags, shared nodes,
512 paths to one leaf, bad windings, missing/mismatched VIS, padding and failed
publication. See [validation results](validation/portal-evidence.json).

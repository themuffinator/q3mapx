# Visibility compilation

```sh
q3mapx -threads auto -vis -reproducible -saveprt map.bsp
q3mapx -threads auto -vis -reproducible -merge -saveprt map.bsp
q3mapx -threads auto -vis -reproducible -mergeportals -saveprt map.bsp
```

VIS reads the BSP and its matching PRT, then writes visibility rows using the
existing BSP cluster IDs. Regenerate both files after changing structural MAP
geometry. A PRT with valid counts is not proof that it belongs to a particular
BSP; retain the outputs from the same BSP compilation.

The default solver combines passage and portal flow. `-nopassage` selects portal
flow, `-passageOnly` selects passage flow, and `-fast` uses preliminary flood
visibility. `-reproducible` fixes result-publication order across worker counts
for the selected mode; it does not make different modes equivalent. `-saveprt`
keeps the PRT for another run. Without it, the PRT is removed only after the BSP
has been written successfully. A reported output failure preserves both the old
BSP and the PRT needed to retry. Checked BSP writers now close their streams and
remove unfinished `.q3mapx-*.tmp` files before reporting write, seek, flush or
publication errors. See [output safety and limits](OUTPUT-SAFETY.md).

## Existing merge options

Use `-bsp-evidence -portals matching.prt map.bsp` for read-only
[regional portal and stored-PVS cost diagnostics](PORTAL-ANALYSIS.md). This shares
VIS's bounded PRT1 parser, retains both inputs and does not run a transformation.

`-merge` combines neighboring convex cells without crossing hint boundaries,
then combines compatible coplanar portal polygons. `-hint` is an alias for this
mode. `-mergeportals` performs only polygon merging. All remain explicit options;
ordinary VIS does not enable merges automatically.

Hint and sky flags are now retained on both directed sides of each PRT opening.
A second unhinted opening cannot bypass a hint between the same two cells.
Polygon joins require the same neighbor and hint/sky flags, matching planes,
a shared edge and the correct winding orientation for convexity. Oversized
proposed unions remain separate: each leaf retains at most 1,024 portals/faces,
and each merged winding at most 512 points. The final winding count, rather than
the sum of the two input counts, determines whether it fits.

These repairs intentionally change affected legacy merge and far-plane/sky
results. The inherited geometric tolerances remain. Merge modes can change the
potentially visible set and do not have a per-region runtime visibility budget.
They are **not** the planned intelligent optimizer or a safe substitute for
correct source detail classification. The [optimizer design](COMPILER-OPTIMIZATION.md)
requires separate baseline-inclusion, geometry and runtime-cost gates before
automatic regional changes can be offered.

The [exact fixture qualification audit](VIS-QUALIFICATION.md) now explains all
84 distinct dropped pairs across four solvers and these merge selections. Coupled
3D endpoint/blocker constraints resolve the last pair, with independent clipping
and slab-inequality replay. The contract covers interior occlusion for known axial
source geometry. General geometry and boundary behavior remain outside its scope;
automatic merge qualification stays closed.

## Compact working data

Deleted portals no longer occupy bits in portal-front, flood, final-flow or
passage bitsets. Dense indices are assigned to the surviving directions after
the selected merges. Portal objects, their original ordering, sort tie breaks
and the fixed 64-job publication batches stay in place. This is a relabeling of
working bits, not a topology or output-format change. Original runtime cluster
rows still include every represented member of a merged cell.

The log reports original and compact bytes per bitset. Storage rounds up to
64-bit words, so deleting a few portals need not save a whole word. Visibility
totals count each row's self bit once.

Passage construction now intersects the two preliminary flood bounds word by
word and visits only their set bits, in the original order. Empty intersections
need no separator construction. Both passage solvers use a packed contiguous
span of nonzero-boundary words per passage; words outside the span remain zero,
including when a recursive scratch frame is reused. One block per source portal
holds the descriptors and mask payloads. These blocks are freed after flow joins,
before assembling/writing BSP visibility. Packing retains fixed reproducible job
order; representation-only reference comparisons require identical output bits
with `-reproducible`. Ordinary scheduling retains its existing variability.

The passage log compares **retained requested bytes** with the preceding dense
linked-list representation and reports empty masks, retained blocks and candidate
visits. Construction initially reserves the dense upper bound for each portal
being built, then shrinks its block. A failed shrink keeps the original valid
block and reports its full requested size. These figures exclude allocator
overhead, the bounded per-job scratch, temporary reservation and other compiler
state; they are not measured peak process memory.

## Large portal clipping

Passage construction clips the **complete** input winding, including all 512
points supported by PRT input. Two reusable buffers per worker replace the
inherited 24-point prefix copy. Intermediate convex polygons may grow while
clipping: capacity is 512 input points plus 1,024 possible separating planes,
since a convex polygon gains at most one vertex per plane. Buffers alternate
without copying the full polygon between cuts and stay off the recursive stack.

If malformed/nonconvex geometry or numerical degeneracy exceeds that bound,
the clipper discards the incomplete output, retains its complete input to that
cut and reports the fallback. Skipping a cut can add visibility; dropping an
arbitrary prefix could wrongly hide visible geometry. Later planes can still
reject the retained polygon. The original epsilon, plane order, axial-coordinate
correction and all-on-plane behavior remain. The recursive separator cache also
accepts its last valid entry, rather than rejecting exactly 512 separators.

These repairs intentionally change affected legacy results. In an analytic
three-opening chain, the old default/passage-only solver could hide a directly
visible cluster solely because of the first vertex chosen for a 64-point polygon.
Rotated/reversed 24–512-point controls now retain independently expected visibility,
including blocked paths; a sealed MAP corridor exercises real 64-point BSP portals.
See [validation](validation/vis-clipping.json). The separate recursive portal
clipper still conservatively retains the original polygon when its small output
buffer fills; this repair does not claim exact general visibility geometry or
resolve the automatic-merge baseline-inclusion gate.

The reference comparison covers all four solvers, default and three merge
selections, and matched structural/manual-detail fixtures. Bitsets and passage
allocations can shrink without a comparable reduction in total command time:
merge analysis, clipping, I/O and startup remain. See the measured results in
[performance](PERFORMANCE.md) and [validation](validation/vis-portals.json).

Windows runs exposed intermittent long delays outside the measured VIS passes,
at both 20 and 70 workers, in preceding executables as well as the current build.
A completed 70-worker comparison retains byte parity, but that separate
performance issue remains under investigation. The benchmark retains slow
samples; working-bit compaction does not claim to repair those delays.

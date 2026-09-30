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

## Compact working data

Deleted portals no longer occupy bits in portal-front, flood, final-flow or
passage bitsets. Dense indices are assigned to the surviving directions after
the selected merges. Portal objects, their original ordering, sort tie breaks
and the fixed 64-job publication batches stay in place. This is a relabeling of
working bits, not a topology or output-format change. Original runtime cluster
rows still include every represented member of a merged cell.

The log reports original and compact bytes per bitset. Storage rounds up to
64-bit words, so deleting a few portals need not save a whole word. Passage
memory diagnostics count all live directions and use wide arithmetic; the
reported allocation excludes allocator overhead and other solver/process memory.
Visibility totals count each row's self bit once.

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

# Performance and reproducibility

All timings below include process startup, file loading, work, and output writing,
unless explicitly described as microbenchmarks. Windows x64, GCC 15.2.0 release,
i7-13700H, 20 logical CPUs; no global fast-math or LTO. Synthetic maps contain no
commercial assets. Results describe these workloads, not every map or machine.

## Stable meta surfaces and patch data

Meta triangles now group materials by immutable shader name, with first-triangle
encounter order breaking ties between distinct identities with the same name.
Merge eligibility still uses the original shader identity and surface properties.
Previously, ordering the shader pointers could permute entire material groups
when unrelated shader allocations changed, affecting surface IDs, index sharing
and subsequent lightmap packing. Adding unused shaders now leaves the tested
outputs unchanged. Existing duplicate-definition precedence is retained.

MAP patch vertices also initialize every channel before parsing authored position
and texture coordinates. Their previously uninitialized lightmap coordinates could
be serialized directly or interpolated into converted patch geometry. The repair
initializes those coordinates without changing positions, texture alignment,
colors, generated normals or patch tessellation in the reference fixtures.

The `meta_order` regression compiles 56 IBSP/RBSP variants and completes eight
BSP/VIS/LIGHT pipelines, varying unused shader allocations, definition order and
one/four workers. Every stored lump is compared; only file gaps, including the
unused writer timestamp, are outside that comparison. The generated model grid
and material/patch room also have identical BSP and final baked lump payloads
between the tested Windows and Linux Release builds. This is fixture evidence,
not a guarantee of byte identity across all maps, options, toolchains or formats.
VIS checks use `-reproducible`; ordinary VIS retains its documented scheduling
behavior. PRT files are byte-identical within each platform and differ only by
native CRLF/LF newlines between these Windows and Linux fixtures.

The preceding Windows compiler produced two grid layouts and eight material/patch
layouts across the same variants. Optional `--reference` checks normalize global
surface allocation and previously uninitialized, unassigned pre-LIGHT UVs only;
all per-surface geometry, texture/color/normal data, model ownership, leaf
associations, other lumps and portal files must match. Current-to-current checks
mask no lump fields. Old/new final lightmap packing and renderer output are not
claimed identical, and this repair makes no compile-speed claim. Rebuilding a map
can change its surface IDs relative to earlier releases, so regenerate reports or
surface-ID exclusions for the newly compiled BSP.

See [validation evidence](validation/meta-order.json) and the
[test instructions](DEVELOPMENT.md#compiler-order-validation).

## Shared BSP index validation

Surfaces may share all or part of the same stored triangle-index array while
using different local vertex counts. The previous validator walked every index
for every referencing surface. A modest file could therefore force billions of
repeated checks before any recovery/evidence work budget applied.

The validator now keeps direct scans for small slices and ordinary disjoint work.
Repeated long scans trigger a blocked maximum tree over the immutable source.
Negative values remain invalid, and unused values outside the requested slice
do not cause rejection. Queries retain each surface's own vertex limit and return
the first offending reference, preserving surface order and existing diagnostics.
The cache stores two unsigned maxima per 64 source indices, rounded to the final
block: approximately 3.125% extra index storage when needed, none for direct scans.

On the generated 20,000-surface/120,000-index stress fixture (2.4 billion implied
references), the complete `-info` command measured **0.8392 → 0.0167 seconds**
median, **50.3× faster**. One warmup and five measured runs alternated execution
order, with identical native statistics and preserved input. This is an adversarial
range-sharing workload, not evidence of a 50× compiler or ordinary map-loading
speedup. The [raw observations](benchmarks/index-validation-win-x64.json) include
source/executable hashes, arguments and timing spread.

Four private native-map comparisons show small, inconsistent timing changes:

| Map | Previous median | Current median |
| --- | ---: | ---: |
| Jedi Academy `t3_stamp` | 26.47 ms | 27.25 ms |
| Allied Assault `m1l1` | 20.39 ms | 19.34 ms |
| F.A.K.K.2 `towncenter_good` | 23.19 ms | 21.73 ms |
| Quake III IHV `tim_dm1` | 13.29 ms | 13.52 ms |

These samples demonstrate native compatibility and do not establish a general
loading speedup. [Native results](benchmarks/index-validation-native-win-x64.json)
retain observations and identities without proprietary geometry. No binaries
changed during measurement and no other task-controlled tests/builds ran alongside
these timing samples. Reproduce with:

```sh
python benchmarks/index_validation.py --baseline /path/to/previous/q3mapx --compiler build/release/bin/q3mapx --work-dir build/index-validation-benchmark
```

Add `--map /path/to/map.bsp --game PROFILE` for a private native copy. The command
measures startup, native load/validation and statistics printing; it does not bake
lighting or evaluate MAP reconstruction. Deterministic unit checks separately
bound source-value visits and storage on varied overlapping slices. Both those
checks and native error/output regressions are recorded in the
[validation evidence](validation/index-validation.json).

## Indexed minimaps

The inherited sampler inspected every opaque brush at every sample. q3mapx builds
a compact two-dimensional spatial index once and evaluates only candidate brushes.
Brush order within each cell is preserved, so summation remains stable. Bounds are
derived from plane equations rather than an assumed ordering of brush sides.
Nonaxial geometry uses conservative candidate bounds.

A 968-brush map, 2048² pixels, four samples and 20 workers took **2.5647 s** median
with the imported NRC executable and **0.0862 s** with indexed CPU sampling:
**29.8x faster for this complete minimap command**, with byte-identical TGA output.
One warmup and five measured runs alternated execution order.
[Raw results](benchmarks/minimap-cpu-win-x64.json).

```sh
q3mapx -threads auto -minimap -backend cpu -size 2048 -samples 4 map.bsp
q3mapx -threads 4 -minimap -backend reference -size 2048 -samples 4 map.bsp
```

`reference` exhaustively visits brushes using the same plane-intersection rules.
It is useful for parity checks. `cpu` uses the spatial index. `auto` uses the CPU
below 256 million pixel-samples and attempts GPU execution above that threshold.

`-random N -seed N` uses a per-pixel deterministic generator, so thread count no
longer changes random sample locations. This intentionally changes the inherited
random pattern. Fixed `-samples` patterns retain the existing distribution.
Image sizes are limited to 1..8192, fixed samples to 1..256, random samples to
1..4096, and borders to 0..0.49; all numeric values must be finite and valid.

## CPU jobs

### VIS merge foundation and live bitsets

The [merge audit](VIS.md) fixes hint/sky direction loss, incorrect convexity,
unchecked leaf unions and oversized winding allocation. After merging, live
portal bits are packed while original jobs and reproducible publication batches
remain unchanged. Complete visibility bytes match a preserved compiler containing
the same merge repairs with uncompressed bitsets. No new automatic regional
optimizer is enabled by these changes.

On a generated 9×9 structural-pillar room, `-merge` leaves 1,348 of 1,436 portal
directions active. Each working bitset falls from **184 to 176 bytes**. Requested
passage storage falls from **3,469,600 to 3,330,816 bytes** (4.0%); this excludes
allocator overhead and other process memory. Runtime VIS remains 7,048 bytes for
220 original clusters. Whole-command medians from one warmup and five alternating
measurements are:

| Mode | Uncompressed, 1 worker | Compact, 1 worker | Uncompressed, 20 workers | Compact, 20 workers |
| --- | ---: | ---: | ---: | ---: |
| Default | 2.4217 s | 2.3698 s | 0.4030 s | 0.3573 s |
| Merge | 2.0696 s | 2.0533 s | 0.3406 s | 0.3392 s |
| Merge portals | 2.4281 s | 2.4042 s | 0.3721 s | 0.3708 s |
| Hint | 2.0758 s | 2.0222 s | 0.3319 s | 0.3277 s |

These results do **not** establish a broad speedup. There is no packing reduction
in the default/mergeportals runs of this fixture, most differences are small, and
Windows samples include intermittent delays outside the named VIS passes.
Those delays also occur in the preceding executable: some complete commands take
5–21 seconds despite about 0.3 seconds of measured VIS work. All slow samples are
retained. Serial work, output and worker shutdown need separate investigation.

The matched manually detailed source has four clusters/eight directed portals;
default compact VIS takes 0.0383 seconds at one worker. This is a reference built
with known authored detail flags, not an automatic optimization result. Every
cluster sees the same 834 leaf-referenced world triangles in this open-room corpus.
Those world-surface counts do not prove entity visibility or general map fidelity.

The structural fixture's existing merge/hint result adds 84 and omits 27 cluster
pairs relative to default VIS, identically before and after packing. Thus these
modes do not meet the intelligent optimizer's planned baseline-inclusion gate.
The omissions are not independently established as safe tightening. Regional
changes still require the separate geometry and runtime-cost checks in the plan.

[Raw timings, profiles, identities and per-cluster costs](benchmarks/vis-portals-win-x64.json)
cover both fixtures, all four merge selections and the preceding executable.
The task ran no other compiler/build/test processes alongside these measurements.
Reproduce with `benchmarks/vis_portals.py --compiler ... --reference ... --legacy ...
--work-dir ...`; `reference` must contain the merge repairs before bitset packing.
This benchmark covers complete VIS commands, not complete BSP/VIS/LIGHT builds
or renderer FPS.

### Earlier scheduling and VIS measurements

Persistent workers and batched atomic range dispatch reduce tiny-job overhead.
The scheduler microbenchmark improved roughly 49.6x, but the initial full-stage
comparison did **not** show a general BSP/VIS/LIGHT speedup. One-worker VIS was
slower in that initial run. See the [task log](PROGRESS.md)
and [raw job-stage results](benchmarks/jobs-win-x64.json). The portal-publication
race fix is retained regardless of benchmark results.

An alternating follow-up on a 9x9-pillar fixture measured NRC **2.6156 s**, the
previous q3mapx build **2.6636 s**, and optimized VIS **2.2991 s** with one worker:
12.1% less elapsed time than NRC, with identical visibility bytes. The changes use
64-bit bitset intersections, reuse recursion scratch on worker-local heaps, and
visit each leaf only once during the initial reachability flood. Scratch recursion
now has a checked 1,024-level limit instead of overflowing the native stack.
[Raw observations](benchmarks/vis-win-x64.json) contain warmups, three alternating
measurements, executable hashes and output comparisons.

The same test exposed an inherited scheduling dependency: NRC's 20-worker output
varied by zero to two visibility bits between runs; existing q3mapx also varies.
Those multithreaded timings (NRC 0.2892 s, q3mapx 0.2573 s median) are characterization,
**not** evidence of byte-identical multithreaded parity. The smaller grid=5 regression
is identical across workers, which did not expose this larger-case limitation.
Use `-vis -reproducible` when repeatability across worker counts is required. It
uses stable portal ordering and fixed 64-job publication batches: workers can
prune against only fully completed earlier batches. All three precise VIS modes
passed repeated grid=9 comparisons at 1, 4, 20 and 70 workers. This mode can take
longer and can differ from the legacy schedule; it does not promise identical
floating-point output across architectures or compiler versions. The earlier
grid=11 timing run is not directly comparable to grid=9.

Use `-profile report.json` to inspect named CPU passes. Profile timing includes
separate execution and setup-inclusive values. See [development](DEVELOPMENT.md)
for reproducing benchmarks and interpreting their limits.

Lighting now gives each job its own repeatable random stream and publishes
per-surface bounce lights in a fixed order. Shared statistics use atomic updates
and batched culling reductions. This fixes output/counter races; it is not itself
a general speedup. The dense material benchmark measured a 2–4% increase in
surface-lighting time while retaining exact output (grid disabled for comparison
with the old random sequence). [Raw results](benchmarks/light-jobs-win-x64.json).
Reproduce with `benchmarks/lighting.py`; omit `--no-grid-lighting` when comparing
builds that use the same grid sampling. Each run restores the same unlit BSP.

Lightmaps now bound mapped sample positions in 8x8 tiles and skip initial samples
outside each light's reach. The bounds include nudged/curved sample positions and
a conservative floating-point margin. Adaptive refinement, material tracing,
filtering and accumulation retain their traversal and order. Small workloads
avoid index construction; `-light -no-light-culling` selects the comparison path.
Polygon lighting also uses compact scratch storage for common polygons, with
safe dynamic storage for large windings and their closing vertex.

On the dense grid=21 material fixture, `-light -fast -samples 4 -bounce 1` took
**2.5814 → 2.2606 s** with one worker (**12.4% less time**) and
**1.1659 → 0.9062 s** with 20 workers (**22.3% less time**). These are whole-process
medians from five alternating runs after warmup, compared with the preceding
deterministic-lighting build `dda68d7`, including lightgrid computation. All four
lighting lumps were byte-identical on every run. This is a synthetic material
fixture result, not a claim for every map. [Raw measurements](benchmarks/light-culling-win-x64.json).

## MAP recovery measurements after 0.3.0

MAP/report publication now uses checked, buffered staging. A 47×47 generated room
with 2,216 brushes and 13,296 recovered faces produced identical MAP bytes and
JSON metadata before/after this change at 1, 4 and 20 requested workers. Five
alternating measurements followed warmup for each worker count. At one worker,
whole-command medians were 0.1553 and 0.1513 seconds; individual measurements and
the other worker counts vary enough that this does **not** establish a speedup.
The change protects existing outputs on reported failures. Its buffer size alone
is not an optimization result, and brush reconstruction remains serial.

[Raw observations](benchmarks/recovery-output-win-x64.json) include executable and
input hashes, exact output comparison, counts and all samples. Reproduce with
`benchmarks/recovery.py`; optional private native inputs use read-only game assets.
Further recovery optimization needs pass-level measurements and the same output
checks before changing reconstruction or scheduling.

Private native comparisons also preserve exact MAP/report output for Jedi Academy
`t3_stamp` (4,118 brushes, 26,791 faces) and Allied Assault `m1l1` (2,711 brushes,
16,170 faces) at all three worker counts. These whole-command samples include
installed asset loading and both files' publication. Timing changes vary by run
and worker-count group, so no consistent native recovery speedup is claimed.
[Native observations](benchmarks/recovery-output-native-win-x64.json) retain hashes,
counts and measurements without distributing the maps or their recovered geometry.

## Raven lightgrid packing

RBSP/FBSP writers now index approximate lightgrid matches by lighting style and
three discriminating byte components. A bounded deterministic sample selects
independent components; candidate verification retains all 24 color channels,
both circular direction bytes, and NRC's earliest-match ordering. Small grids
keep a direct scan. Dictionary exhaustion fails before replacing the BSP instead
of writing an invalid reference.

On the same Windows machine, a complete `-game ja -scale 1` rewrite of the retail
Jedi Academy `maps/mp/duel9.bsp` took **8.5414 → 0.2705 s**, a **31.6x speedup**.
Its grid has 28,749 dictionary points. Three alternating measured runs followed
one warmup; all eighteen lump payloads were identical. This measures BSP loading,
validation, identity scaling and serialization, not the cost of calculating the
original lighting. The retail input is not distributed. Its hash, grid reference
count, executable identities and observations are in the
[native-map report](benchmarks/lightgrid-native-win-x64.json).

A generated 24,000-entry, 96,000-reference high-variation grid measured
**1.7567 → 0.1286 s** over five alternating runs after warmup, also with identical
payloads. This deliberately stresses dictionary lookup and is not representative
of every lighting distribution. See the
[synthetic report](benchmarks/lightgrid-synthetic-win-x64.json). Reproduce with:

```sh
python benchmarks/lightgrid.py --baseline /path/to/old/q3mapx --compiler build/release/bin/q3mapx --work-dir build/lightgrid-benchmark
```

`--map /path/to/native.bsp` measures a private copy of an existing Raven map; the
source is never rewritten. Existing unused-channel validation limitations found
on other retail maps are tracked in the task log rather than bypassed here.

## OpenCL GPU minimaps

`q3mapx -devices` prints JSON with GPU names, memory, compute units and stable
enumeration indices for that driver configuration. Automatic selection prefers
dedicated GPU memory and then more compute units; `-gpu-device N` overrides it.
Native drivers are preferred over duplicate Windows translation layers.

```sh
q3mapx -minimap -backend gpu -gpu-device 0 -size 4096 -samples 16 -compute-report compute.json map.bsp
```

At 4096² pixels and 16 samples, the RTX 4060 Laptop GPU measured **0.3808 s** median
versus **0.7364 s** for indexed CPU sampling at 20 workers: **1.93x faster**, including
initialization, uploads, kernel execution, download, CPU postprocessing and file
output. At 2048²/four samples the CPU measured 0.0924 s versus GPU 0.2496 s, so
automatic selection retains the CPU for small jobs. Evidence:
[large workload](benchmarks/gpu-large-win-x64.json),
[small workload](benchmarks/gpu-small-win-x64.json).

The 256-million-pixel-sample threshold is conservative for these warm driver-cache
measurements, not universal calibration for every device/map. The first kernel
compilation on this machine took about 0.96 s; cold caches can change the crossover.
Use explicit CPU/GPU selection and reports when tuning a different machine.
`auto` falls back on loader/device/allocation/build/dispatch/readback failure.
Explicit `gpu` returns an error rather than silently claiming GPU execution.

Real NVIDIA and Intel native-driver tests cover fixed/random samples, borders,
contrast and sharpening. Output differed by at most one 8-bit grayscale level.
[Hardware parity evidence](benchmarks/gpu-parity-win-x64.json). Float contraction
is disabled; driver floating-point differences can still affect samples very near
geometric boundaries. Report unexpected parity failures with the input/options.

Lighting defaults to CPU. Its [experimental GPU area-factor implementation](GPU-LIGHTING.md)
retains material tracing on CPU. Complete-bake measurements, rather than kernel
throughput, determine whether GPU selection is useful for a given workload.

## Passage construction and retained storage

Both passage solvers now enumerate the intersection of preliminary flood masks,
skip separator construction for empty intersections and store trimmed word spans
in one block per source portal. Recursive flow explicitly clears omitted words
in reused scratch. These packing changes were validated before the separate
large-portal clipping repair described below; they retained the same graph,
clipping order, solver choice and visibility bytes.

On the structural grid=9 fixture (220 clusters, 718 undirected portals), retained
requested passage storage changes as follows:

| Graph | Dense bytes | Packed bytes | Reduction | Dense portal visits | Candidate visits |
| --- | ---: | ---: | ---: | ---: | ---: |
| Default | 3,004,400 | 1,284,000 | 57.3% | 21,571,592 | 2,386,961 |
| Existing `-merge` | 3,330,816 | 1,311,152 | 60.6% | 23,385,104 | 2,463,959 |

The default case holds 15,022 passage descriptors, including 3,029 empty masks,
in 1,436 blocks. The merged case holds 17,348 descriptors in 1,348 blocks. These
are retained requested bytes, excluding allocator overhead and other compiler
data. A concurrently building portal initially reserves its dense upper bound,
then shrinks the block; this is **not** a peak-RSS measurement. All passage blocks
are released after flow, before leaf-row assembly and BSP output.

Five alternating whole-command measurements follow one warmup on Windows x64,
against the preceding `e2965d0` executable. Every output matches its reference and
worker-count counterpart; these checks do not equate different solver/merge modes.

| Graph/solver | Workers | Dense median (s) | Packed median (s) |
| --- | ---: | ---: | ---: |
| Default/full | 1 | 2.3900 | 2.3654 |
| Default/full | 4 | 0.7169 | 0.7125 |
| Default/passage-only | 1 | 0.2932 | 0.2427 |
| Default/passage-only | 4 | 0.1204 | 0.1306 |
| Merge/full | 1 | 2.0438 | 2.0131 |
| Merge/full | 4 | 0.6239 | 0.6270 |
| Merge/passage-only | 1 | 0.2437 | 0.2053 |
| Merge/passage-only | 4 | 0.1077 | 0.1126 |

Single-worker passage-only improves by 17.2% without merging and 15.8% with
merging on this fixture. Full-flow changes are small; four-worker passage-only
whole-command samples are slower despite lower measured construction time.
There is no general speedup claim. For default/full at one worker, median
`CreatePassages` time with setup falls 0.09434→0.07005 seconds, while the roughly
2.17-second geometric flow dominates. Tiny manually detailed controls are mostly
startup/scheduling noise and are retained in the [raw measurements](benchmarks/vis-passages-win-x64.json).

Reproduce with `benchmarks/vis_passages.py --compiler /path/to/current/q3mapx
--reference /path/to/e2965d0/q3mapx --work-dir build/passage-benchmark`. Use the
same toolchain, options, hardware and input generation for both binaries.
The report includes hashes, all samples, pass profiles, storage and candidate
counts. This does not validate automatic topology changes: the separate
baseline-inclusion failures of existing merge modes remain open.

## Large-portal clipping repair

The passage clipper now retains complete input polygons and allows bounded
intermediate growth instead of truncating at 24 points. This is a correctness
repair: old visibility bytes are intentionally changed for affected large
portals. The [analytic/native validation](validation/vis-clipping.json) is
separate from timing unchanged ordinary-map workloads. Reusable per-worker
scratch avoids per-candidate allocation and alternates buffers instead of copying
the entire polygon after each cut. Mask storage figures above exclude this
scratch and are not a peak-memory measurement.

Use `benchmarks/vis_passages.py --reference-storage packed --reference
/path/to/ac402aa/q3mapx --compiler /path/to/current/q3mapx --work-dir
build/clipping-benchmark` to compare two revisions that both use packed masks.
This mode requires identical visibility and storage accounting for the measured
fixtures. The original dense-reference mode remains the default.

On Windows x64, five alternating observations after warmup cover the same
grid=9 structural/manual-detail fixtures, both passage solvers, normal/merged
graphs and one/four workers. All 192 commands retain reference/worker VIS bytes,
unchanged native non-entity/non-VIS lumps and identical passage accounting.
The structural complete-command results are seconds, with median (minimum–maximum):

| Graph/solver | Workers | `ac402aa` | Complete-winding clipping |
| --- | ---: | ---: | ---: |
| Default/full | 1 | 2.3613 (2.3271–2.3981) | 2.3189 (2.3128–2.3762) |
| Default/full | 4 | 0.6891 (0.6633–0.7560) | 0.6828 (0.6435–0.6855) |
| Default/passage-only | 1 | 0.2273 (0.2003–0.3196) | 0.2318 (0.2179–0.2466) |
| Default/passage-only | 4 | 0.0958 (0.0940–0.1186) | 0.0957 (0.0812–0.1074) |
| Merge/full | 1 | 2.0180 (1.9731–2.0446) | 1.9894 (1.9767–2.0348) |
| Merge/full | 4 | 0.6198 (0.5673–0.6473) | 0.5998 (0.5845–0.6437) |
| Merge/passage-only | 1 | 0.2429 (0.2256–0.2544) | 0.2320 (0.2195–0.2788) |
| Merge/passage-only | 4 | 0.1318 (0.0987–0.1530) | 0.0919 (0.0841–0.1217) |

Default/full single-worker `CreatePassages` time with setup falls from 0.07115
to 0.06454 seconds median. Complete-command changes are mixed: default single-
worker passage-only is 2.0% slower, while full-flow medians are slightly lower.
Tiny manual-detail controls have substantial process/scheduling variation.
These are fixture-specific observations, not a general compiler speedup or
peak-memory claim. All observations, including slow samples, profile timings
and compiler/input identities, remain in the [raw measurements](benchmarks/vis-clipping-win-x64.json).

## UV consensus recovery cost

The default [multi-triangle recovery](UV-RECOVERY.md) adds evidence collection,
affine checks/fitting and bounded per-face reporting. This is an accuracy feature
with measured additional cost. The following `9bb349c` measurements precede the
subsequent offset/precision serialization repair. Windows x64 runs on an Intel Core
i7-13700H use one worker, Valve 220 output and five alternating observations per
policy after one warmup. Runs were sequential after the build/test jobs finished.

| Input | Exported faces | Triangle median (s) | Consensus median (s) | Added cost |
| --- | ---: | ---: | ---: | ---: |
| Generated 47×47 room | 13,296 | 0.1605 | 0.1789 | 11.5% |
| Private Jedi Academy sample | 26,791 | 0.9144 | 0.9990 | 9.3% |
| Private MOHAA sample | 16,170 | 0.3708 | 0.3815 | 2.9% |

These whole-command medians include resource lookup, recovery and MAP/report
publication; they are neither kernel timings nor general scaling predictions.
Each policy produces identical MAP/report bytes on repeated runs. Triangle-mode
MAPs exactly match the preceding `3df5fd2` compiler, and consensus changes only UV
definitions in the exported MAP text. These checks do not establish original-map
UV fidelity for the private samples. The generated room also exercises report
truncation: 10,000 retained face records and 1,057 explicitly omitted records.

Reproduce with `benchmarks/uv_recovery.py`; see [test instructions](DEVELOPMENT.md#multi-triangle-uv-recovery).
All observations, compiler/input identities, decisions and limitations are in the
[validation record](validation/uv-consensus.json). Private native inputs and assets
are not distributed. Timing variation, especially on short commands, remains
visible in the individual observations.

With full texture offsets and precise serialization, a fresh generated-room run
measures 0.1638 seconds for the current triangle compatibility path and 0.1953
seconds for current consensus output (five alternating samples after warmup,
one worker on the same machine). The complete-command difference is 31 ms or
19.2%; it includes consensus analysis/reporting and is not an isolated measurement
of decimal formatting cost. All observations and identities are retained in
[output validation](validation/uv-output.json). MAP/report output remains
repeatable, the compatibility MAP matches the pre-consensus reference, and
non-UV structure agrees, with patch positions compared as parsed binary32 values.
This does not claim a speedup, arbitrary-map fidelity, or newly measured private-map
performance for the revised writer.

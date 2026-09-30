# Performance and reproducibility

All timings below include process startup, file loading, work, and output writing,
unless explicitly described as microbenchmarks. Windows x64, GCC 15.2.0 release,
i7-13700H, 20 logical CPUs; no global fast-math or LTO. Synthetic maps contain no
commercial assets. Results describe these workloads, not every map or machine.

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

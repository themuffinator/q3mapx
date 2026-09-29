# Performance and reproducibility

All timings below include process startup, file loading, work, and output writing,
unless explicitly described as microbenchmarks. Windows x64, GCC 15.2.0 release,
i7-13700H, 20 logical CPUs; no global fast-math or LTO. Synthetic maps contain no
commercial assets. Results describe these workloads, not every map or machine.

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

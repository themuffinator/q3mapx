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
Use one VIS worker when exact reproducibility is required pending the explicit
reproducible mode. The earlier grid=11 run is not directly comparable to grid=9.

Use `-profile report.json` to inspect named CPU passes. Profile timing includes
separate execution and setup-inclusive values. See [development](DEVELOPMENT.md)
for reproducing benchmarks and interpreting their limits.

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

Lighting remains on CPU. Its [GPU evaluation](GPU-LIGHTING.md) describes measured
costs and the material semantics that a future implementation must preserve.

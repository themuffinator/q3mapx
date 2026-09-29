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
It is useful for parity checks. `cpu` uses the spatial index. `auto` currently uses
the CPU; GPU selection will be enabled only with measured crossover evidence.

`-random N -seed N` uses a per-pixel deterministic generator, so thread count no
longer changes random sample locations. This intentionally changes the inherited
random pattern. Fixed `-samples` patterns retain the existing distribution.
Image sizes are limited to 1..8192, fixed samples to 1..256, random samples to
1..4096, and borders to 0..0.49; all numeric values must be finite and valid.

## CPU jobs

Persistent workers and batched atomic range dispatch reduce tiny-job overhead.
The scheduler microbenchmark improved roughly 49.6x, but the initial full-stage
comparison did **not** show a general BSP/VIS/LIGHT speedup. One-worker VIS was
slower in that run and needs an alternating investigation. See the [task log](PROGRESS.md)
and [raw job-stage results](benchmarks/jobs-win-x64.json). The portal-publication
race fix is retained regardless of benchmark results.

Use `-profile report.json` to inspect named CPU passes. Profile timing includes
separate execution and setup-inclusive values. See [development](DEVELOPMENT.md)
for reproducing benchmarks and interpreting their limits.

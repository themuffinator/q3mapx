# Experimental GPU area-light factors

CPU lighting remains the default. `-light -light-backend gpu` enables an
experimental hybrid implementation: OpenCL computes batched point-to-polygon
form factors for initial area-light samples on the four largest eligible raw
lightmaps. The CPU retains shadow rays, material filtering, alpha shadows,
sun/point/spot lights, adaptive refinement, lightgrid and vertex lighting.
This is **not GPU ray tracing** and has not demonstrated a complete-bake speedup
on the measured material fixtures. The existing GPU minimap backend is separate.

```sh
q3mapx -threads auto -light -fast -samples 4 -bounce 1 -light-backend gpu -compute-report lighting.json map.bsp
q3mapx -threads auto -light -fast -samples 4 -bounce 1 -light-backend cpu map.bsp
```

`-gpu-device N` chooses a device from `-devices`; otherwise selection prefers a
native discrete device with double precision. OpenCL FP64 is required to preserve
NRC's mixed-precision normalization and polygon integration. The tested RTX 4060
Laptop driver supports it; the tested Intel Iris Xe driver does not. A requested
GPU that cannot initialize fails visibly before baking. Ordinary CPU lighting
needs no OpenCL runtime.

The context/program is reused across lightmaps and bounces. Light groups stream
through at most 64 MiB of output per selected raw lightmap, rather than allocating
an unbounded light-by-sample matrix. At most four caches exist concurrently; input
samples add up to 32 MiB each. Kernel dispatches bound polygon-edge work and device
buffers are released after each batch. Allocation/dispatch failure switches the
affected batch to CPU and records the reason. `-faster`, ineligible lightmaps and
windings beyond 1,024 vertices retain their CPU calculation.

The JSON report distinguishes `hybrid` from `cpu`, records actual batch/factor
counts, fallback batches and setup/upload/kernel/readback timings. Its compute
time is the sum of serialized API calls; use `-profile` or whole-command timing
for the cost including packing, waiting, CPU tracing and file output. No automatic
lighting GPU threshold is claimed from a small fixture corpus.

Regression checks compare polygon math through 1,024 points, repeated batches,
invalid ranges and device failures. Actual bakes cover alpha shadows, colored
transmission, sun/sky, emitters, brush origins, patches, bounces, deluxemaps,
supersampling, randomized dirt/samples and floodlighting. A dense case exercises
multiple streamed groups for one lightmap. Numerical checks use an absolute
factor tolerance of 1e-6; the tested NVIDIA results and baked lighting data match
the CPU exactly. That does not promise identical arithmetic on every driver.

## Complete-bake measurements

Windows, i7-13700H, RTX 4060 Laptop, one warmup per implementation/worker count;
execution alternated and every bake compared all four lighting lumps exactly.

| Fixture/options | Workers | CPU median | Hybrid median |
| --- | ---: | ---: | ---: |
| Dense grid=21, `-fast -samples 4 -bounce 1`, 5 runs | 1 | 2.2287 s | 2.5518 s |
| Same | 20 | 1.0780 s | 1.4624 s |
| Dense grid=9, `-samples 4 -bounce 1`, 3 runs | 1 | 18.4584 s | 18.3714 s |
| Same | 20 | 1.2532 s | 1.8160 s |

The full-envelope single-worker difference is too small relative to the spread
to establish a speedup. Other cases are slower. Driver setup and host work remain
material costs; fast GPU kernels alone do not shorten the bake. The grid=21 case
computed 44,211,000 factors in ten streamed batches without fallback. CPU time
still includes material traces, refinement and lightmap processing.

Raw records include warmups, executable/input hashes, profiles, GPU timing and all
individual observations: [fast](benchmarks/light-gpu-fast-win-x64.json) and
[full-envelope](benchmarks/light-gpu-accurate-win-x64.json). Runs show variance,
particularly at 20 workers; the first hybrid warmups took 11.33 and 37.93 seconds,
respectively. These results justify keeping CPU as the default and treating this
backend as an opt-in research path, not advertising a lighting speedup.

Those GPU comparisons preceded the final separation of CPU/GPU inner-loop
dispatch. That separation removes per-sample cache checks from default CPU bakes.
A subsequent five-run CPU-only comparison found no regression against the
pre-GPU build: 2.2600/2.2061 s at one worker and 0.9222/0.8896 s at 20 workers.
[Dispatch validation](benchmarks/light-gpu-cpu-dispatch-win-x64.json). Kernel math,
batch limits and tracing behavior are unchanged; material parity was rechecked.

Full GPU tracing remains future work. `light_trace.cpp` accumulates material flags,
distinguishes sky/skybox paths, filters ray color through texture pixels, requests
texture-dependent subsampling and observes model/shadow rules. A boolean hit test
cannot replace it. The implemented hybrid path leaves these semantics intact.

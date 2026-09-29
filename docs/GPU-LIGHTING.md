# Lighting GPU evaluation

The implemented GPU workload is minimap column sampling. No lighting option
currently uses GPU tracing, and the GUI must label GPU selection accordingly.

A 968-brush synthetic room with 4,818 surfaces was compiled at 20 workers using
`-light -fast -samples 4 -bounce 1`. Total elapsed time was 0.6102 s. The first
`IlluminateRawLightmap` pass took 0.0119 s; the bounced-light pass took 0.3649 s.
Grid tracing took 0.0112 s. [Raw profile](benchmarks/light-gpu-evaluation.json).
This is an exploratory fixture without a VIS pass, not a production-map survey.
The bounced pass is a worthwhile future target; the first direct/grid passes are
smaller than the measured OpenCL initialization cost on this machine.

The current tracer in `light_trace.cpp` is more than opaque triangle occlusion:
it accumulates material flags, distinguishes sky/skybox paths, filters ray color
through texture pixels, applies alpha shadows and light filters, requests texture
dependent subsampling, avoids seam artifacts, and observes model/shadow rules.
Replacing it with a boolean ray hit would change lighting correctness.

A suitable next implementation would separate ray generation, immutable scene
data and result consumption; batch opaque-only rays first; keep material-aware
rays on the CPU until equivalent texture filtering exists; and compare both
individual traces and baked lightmaps. It needs a multi-material fixture corpus,
including alpha textures, colored filters, sun/sky, moving brush-model origins,
bounces and sampling modes. Upload/setup cost must be amortized across passes.

Decision for the initial workbench: retain the tested CPU lighting implementation
and expose profiles. A rushed GPU ray replacement is not justified by this single
profile. GPU lighting is a documented future development area, not shipped
functionality or an implied benefit of the minimap backend.

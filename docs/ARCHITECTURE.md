# Architecture

## Compiler and application boundary

Keep the imported q3map2 compiler in `tools/quake3/` and its required support code
in `libs/` and `include/`. Build it as the `q3mapx` executable. Preserve legacy
command syntax while adding explicitly named q3mapx options. Avoid a wholesale
directory rename so upstream changes remain reviewable.

The Qt 6 workbench will be a separate executable. It launches the compiler using
QProcess with an argument list and observes its output/exit status. This boundary
isolates the legacy compiler's global state and fatal-exit paths from the GUI,
keeps CLI installations lightweight, and permits cancellation without freezing
the application. Persist projects as versioned JSON, not shell scripts.

## CPU execution

Introduce a reusable C++ job implementation behind the existing
`RunThreadsOnIndividual` interface. Preserve the join/barrier semantics expected
by compiler passes. Keep scheduling atomics, progress reporting, and legacy
geometry locks separate. Reuse worker threads across passes; allow a synchronous
one-thread path. Do not parallelize writes to existing shared structures without
first establishing ownership or synchronization.

Batch cheap homogeneous items to reduce dispatch overhead. For costly or uneven
items use fine-grained or shrinking ranges. Publish stage measurements so choices
can be evaluated against complete builds. Keep floating-point behavior stable;
do not enable unsafe global fast-math as a substitute for algorithmic work.

## GPU execution

GPU support uses optional, dynamically loaded OpenCL 1.2 with embedded first-party
kernels and Khronos headers. The executable needs no OpenCL SDK or loader to run
CPU work. Indexed minimap column sampling has both CPU and GPU implementations;
lighting traversal needs additional design because of
alpha-tested shaders, material semantics, and CPU-side state.

Include device information, backend selection, setup/transfer/compute timing,
and failure diagnostics. Avoid retaining driver resources outside an owning RAII
object. Unavailable devices must not prevent ordinary CLI use. Validate numerical
tolerances and small-workload crossover points before enabling automatic GPU use.

Column planes, brush records and spatial candidate lists use matching host/kernel
layouts. Row batches bound individual dispatch size; host-computed sample origins
preserve legacy coordinate precision without requiring device doubles. Floating
point contraction is disabled in the kernel. Missing devices and runtime failures
fall back in automatic mode; explicit GPU mode fails visibly. `-devices` lists JSON
capabilities, and `-compute-report` explains selection and timing.

## Validation and observability

Validate external data before pointer arithmetic, vector allocation, or indexing.
Keep size calculations in checked unsigned arithmetic and validate cross-lump
references. Reports must distinguish warnings, fatal errors, and lossy recovery.
Machine-readable reports use versioned schemas and escape arbitrary paths/text.

Synthetic fixtures are first-party and license-independent. Exercise the real
compiler as a subprocess for integration tests. Unit tests target invariants of
new scheduling, validation, and application logic. Benchmark data records the
revision, hardware, thread/backend settings, and repeated elapsed measurements.

## Decisions recorded at project start

| Decision | Rationale |
| --- | --- |
| NRC master pinned by SHA | Reproducibility while starting from the latest source at retrieval |
| CMake + Ninja, C++20 | Standalone cross-platform build consistent with upstream language requirements |
| Qt 6 Widgets | Native file/process integration, accessibility, high DPI, and mature desktop controls |
| Separate CLI and GUI processes | Preserve compatibility and isolate global state/fatal failures |
| GPL-3.0-or-later combined project | Exercise q3map2's later-version permission to accommodate Apache-2.0 ETC code |
| Evidence before acceleration claims | Avoid regressions or optimizations that only help synthetic microbenchmarks |

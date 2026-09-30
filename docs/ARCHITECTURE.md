# Architecture

## Compiler and application boundary

Keep the imported q3map2 compiler in `tools/quake3/` and its required support code
in `libs/` and `include/`. Build it as the `q3mapx` executable. Preserve legacy
command syntax while adding explicitly named q3mapx options. Avoid a wholesale
directory rename so upstream changes remain reviewable.

The Qt 6 workbench is a separate executable. It launches the compiler using
QProcess with an argument list and observes its output/exit status. This boundary
isolates the legacy compiler's global state and fatal-exit paths from the GUI,
keeps CLI installations lightweight, and permits cancellation without freezing
the application. Persist projects as versioned JSON, not shell scripts.

## CPU execution

The reusable C++ job implementation sits behind the existing
`RunThreadsOnIndividual` interface. It preserves the join/barrier semantics expected
by compiler passes. Keep scheduling atomics, progress reporting, and legacy
geometry locks separate. Reuse worker threads across passes; allow a synchronous
one-thread path. Do not parallelize writes to existing shared structures without
first establishing ownership or synchronization.

Fatal CLI errors serialize their diagnostic with ordinary output, flush console
and editor feedback, and terminate immediately with status 1. They do not run
global destructors or exit handlers while other workers can still access that
state. Normal completion joins the pool and runs normal cleanup. Fatal paths
still omit CPU profiles and can leave incomplete non-transactional outputs.

VIS uses 64-bit bitset intersections, heap-backed scratch frames reused per worker,
and an iterative initial reachability flood. Optional reproducible mode sorts portal
ties by input index and publishes only completed 64-job batches, so worker timing
cannot change the set of portal masks available for pruning.

Batch cheap homogeneous items to reduce dispatch overhead. For costly or uneven
items use fine-grained or shrinking ranges. Publish stage measurements so choices
can be evaluated against complete builds. Keep floating-point behavior stable;
do not enable unsafe global fast-math as a substitute for algorithmic work.

## GPU execution

GPU support uses optional, dynamically loaded OpenCL 1.2 with embedded first-party
kernels and Khronos headers. The executable needs no OpenCL SDK or loader to run
CPU work. Indexed minimap column sampling has both CPU and GPU implementations;
an experimental lighting backend batches polygon form factors while keeping
alpha-tested shadows, material filtering and all ray traversal on the CPU.

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

Area-light factors use a reusable FP64 OpenCL program. A stable set of four large
raw lightmaps streams groups of lights through bounded caches; workers serialize
device submission and independently consume results with the original CPU tracer.
Failure clears the affected offsets before CPU fallback. The default stays CPU
because complete bake measurements have not justified automatic use. See
[lighting design and limits](GPU-LIGHTING.md).

## Validation and observability

Validate external data before pointer arithmetic, vector allocation, or indexing.
Keep size calculations in checked unsigned arithmetic and validate cross-lump
references. Reports must distinguish warnings, fatal errors, and lossy recovery.
Machine-readable reports use versioned schemas and escape arbitrary paths/text.

Synthetic fixtures are first-party and license-independent. Exercise the real
compiler as a subprocess for integration tests. Unit tests target invariants of
new scheduling, validation, and application logic. Benchmark data records the
revision, hardware, thread/backend settings, and repeated elapsed measurements.

## Planned inference and optimization boundary

The first shared analysis layer, `bsp_evidence.h/.cpp`, exposes native-reader
observations separately from JSON serialization and CLI output publication.
It uses exact unoriented plane groups, bounded iterative world traversal, active
ancestor-plane counts and model ownership difference arrays. Versioned
[evidence reports](BSP-EVIDENCE.md) provide provenance and explicit unavailable
observations. Future stages will add proposals and saved overrides across
[recovery inference](RECOVERY-INFERENCE.md) and
[compiler optimization](COMPILER-OPTIMIZATION.md). Their objectives and application
policies remain separate: recovery proposes authoring hypotheses, while compiler
options apply only transformations that meet visibility or presentation contracts.
Structural participation changes run before portal generation in a full build;
VIS-only processing preserves a conservative mapping to the existing BSP clusters.
Lighting inference uses the game's forward lighting semantics and records how much
of the baked data its hypotheses explain. The read-only analysis command makes
no edits. Optional recovery policies now use bounded brush-cell evidence for
detail proposals and shared rendered surfaces for group proposals; broader
inference, review overlays and automatic compiler transformations remain planned.

Group recovery reuses the per-material triangle bounds index, with stricter plane
and overlap tests than ordinary UV recovery. `recovery_groups.h/.cpp` separates
association components and loader-order planning from compiler globals and MAP/JSON
serialization. The planner prunes incompatible groups, orders accepted groups
deterministically and enforces a work budget. Export accounts for both opaque
brush reversals during parsing and group collapse, while preserving translucent
order. Reports distinguish this mechanical ordering contract from unproven
original grouping, compile parameters and rebuilt presentation.

## Planar reduction boundary

The first triangle-reduction transformation is isolated in
`libs/q3mapx/planar_reduction.*`, with binary32 exact determinant signs in
`exact_predicates.*`. It consumes immutable geometry/32-field vertices and returns
ordered faces plus replayable edits under explicit work/storage bounds. Native
format adapters and renderer/material eligibility are separate planned layers.
It is linked into the shared support library for testing but is not invoked by
the compiler or workbench. See [its contract](PLANAR-REDUCTION.md) before using the
core: exact affine-field preservation alone does not approve a runtime rewrite.

## Decisions recorded at project start

| Decision | Rationale |
| --- | --- |
| NRC master pinned by SHA | Reproducibility while starting from the latest source at retrieval |
| CMake + Ninja, C++20 | Standalone cross-platform build consistent with upstream language requirements |
| Qt 6 Widgets | Native file/process integration, accessibility, high DPI, and mature desktop controls |
| Separate CLI and GUI processes | Preserve compatibility and isolate global state/fatal failures |
| GPL-3.0-or-later combined project | Exercise q3map2's later-version permission to accommodate Apache-2.0 ETC code |
| Evidence before acceleration claims | Avoid regressions or optimizations that only help synthetic microbenchmarks |

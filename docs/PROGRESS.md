# Task log

## 2026-09-29 — Plan and upstream audit

Completed the project plan, architecture, development policy, decompilation design,
and attribution documentation. Verified remote NRC HEAD as
`8216133984031afaa9a857b56ea66dd9c3d54b26` and retrieved that reference source.
Inspected the upstream license and compiler build definitions.

Validation: empty initial repository verified; upstream revision independently
queried and cloned; local build toolchains and dependencies inventoried.

Findings to address:

- NRC creates threads for every parallel pass and serializes each work dispatch
  under the compiler's global lock.
- The active thread implementation has a fixed 64-element array while automatic
  hardware detection is not bounded to that array.
- Decompiler triangle lookup stops at a maximum bound, potentially missing larger
  triangles that still overlap the brush face.
- Upstream uses a monolithic Makefile; a standalone CMake build is needed.

No performance gains or finished GUI/GPU features are claimed at this stage.
Next task: import the required source subset and produce the baseline executable.

## 2026-09-29 — Standalone compiler source import

Imported 209 files from the pinned NRC revision, including compiler sources,
transitive local dependencies, image/network support, upstream regression fixtures,
and license/contributor texts. Added an original-file SHA-256 provenance manifest.
Excluded the Radiant editor and bundled Assimp implementation.

Validation: verified every imported file against the source reference by SHA-256.
Compiler and bundled component license notices were checked for compatibility.
No compiler behavior was changed. Build validation is the next task.

## 2026-09-29 — Component license audit correction

The full file-level audit found Apache-2.0 ETC code and BSD-3-Clause WebP code;
the initial documentation incorrectly described both as MIT. Corrected the labels,
included missing license texts, and selected GPL-3.0-or-later for the combined
project using q3map2's existing later-version permission. Original file notices
remain unchanged. This is an inherited licensing detail, not a compiler change.

Validation: checked actual file notices and Apache's published GPL compatibility
guidance; retained the original import manifest unchanged.

## 2026-09-29 — Standalone release build

Added CMake/Ninja release, debug, and profile presets, explicit system dependencies,
CTest help/game smoke checks, optional LTO and sanitizer switches, and license
installation rules. No imported compiler source changes were needed.

Validation: Windows x64 release built with MSYS2 GCC 15.2.0; 2/2 CTest tests passed.
Preserved the baseline binary at `build/baseline/bin/q3mapx.exe` (SHA-256
`5BEE5356D9B4D0C94A80B6E206C940340DB11F807FE44B416EEB366AD210336E`).
Build log: `.agents/tmp/bootstrap/build.log`. Dependencies: GLib 2.86.2, libxml2
2.15.1, Assimp 6.0.2, PNG 1.6.51, zlib 1.3.1, libjpeg ABI 80.

Additional findings: this MSYS2 Assimp pkg-config file contains non-relocatable
paths; using its CMake config resolves that packaging issue. Upstream produces
warnings for non-standard-layout `offsetof` and direct libxml buffer access.
Linux and MSVC execution are not locally validated yet. Next: asset-independent
integration fixtures and timing baseline.

## 2026-09-29 — Integration fixtures and measured baseline

Added first-party generated textures/maps, full BSP/VIS/LIGHT compilation, Quake,
brush-primitive and Valve 220 decompile/recompile checks, patch/entity preservation,
minimap output checks, and brush-geometry parity at one and four workers. Added a
repeatable end-to-end benchmark and Windows/Linux CI definitions.

Validation: 3/3 local CTest tests passed. Dense-room baseline uses GCC 15.2.0 `-O3`,
LTO off, Intel Core i7-13700H, one warmup and five measured samples per stage and
worker count. Full evidence is in `docs/benchmarks/baseline-win-x64.json`.

| Stage | 1 worker median | 4 workers median | 20 workers median |
| --- | ---: | ---: | ---: |
| BSP | 0.0455 s | 0.0469 s | 0.0501 s |
| VIS | 26.8239 s | 2.2080 s | 0.8867 s |
| LIGHT | 0.0777 s | 0.0582 s | 0.0624 s |
| Decompile | 0.0745 s | 0.0738 s | 0.0464 s |
| Minimap, 512², four samples | 0.1759 s | 0.0580 s | 0.0384 s |

These are inherited-compiler timings. This small lighting workload shows overhead
at high thread counts. Decompilation is mostly serial, so its apparent change with
the thread option should be treated as run-order/system variance. VIS's shared
pruning can change the amount of work with scheduling; its scaling is not a pure
scheduler comparison. No game content, game launch, or input injection was used.
Remote CI has not been executed locally. Next: BSP boundary validation.

## 2026-09-29 — BSP boundary and reference validation

Added checked 17/18-lump headers, overflow-safe range checks, alignment-safe lump
copies, terminated name checks, finite geometry checks, model/brush/surface/leaf
references, patch dimensions, triangle indices, iterative node-cycle/depth checks,
visibility dimensions, entity model references, and RBSP lightgrid indirection.
`-force` still permits version mismatches but cannot bypass memory-safety checks.
Partial loads used by the packager retain their supported behavior.

Bounded fatal-error formatting and removed the unconditional one-second error
delay when no Radiant connection exists. The delay remains for connected clients.

Validation: release build and 4/4 CTest tests pass. The corruption suite exercises
strict and `-force` failures, ordinary and unaligned payloads, classic 17-lump Q3
headers, Quake Live IBSP47, Jedi Academy RBSP (including invalid lightgrid indices),
and partial loading. All corrupt cases return controlled errors rather than crashes.
Full compiler/decompiler pipeline remains passing.

Remaining robustness work includes checked large-file I/O, transactional output
replacement, additional numeric option validation, and broader sanitizer/fuzz
coverage. Existing legacy asset decoders are not covered by these BSP checks.

## 2026-09-29 — Texture recovery and decompilation workflow

Fixed missed large overlapping triangles with a per-material bounds hierarchy,
replaced cancellation-prone texture equations with a checked affine solve, and
precomputed detail-brush membership. Added `-decompile`, selectable `.map` output,
automatic/explicit JSON recovery reports, finite fallback UVs, and diagnostics for
lossy recovery. Existing Quake, brush-primitive, and Valve 220 conversions remain.
Unknown conversion options/formats now fail explicitly.

Validation: release build and 6/6 CTest tests pass. Tests compare recovered visible
UVs, compile all output formats, preserve entities/origins/patches, exercise a large
triangle over a small brush, and verify finite output for constant UVs. The large
triangle regression was run against the preserved NRC executable and reproduced
the wrong texture coordinates there; it passes with q3mapx. Math checks cover large
translations, collinearity, nearly singular geometry, and non-finite inputs.

An alternating seven-run comparison on a 968-brush fixture measured 0.3274 s median
for NRC and 0.3259 s for q3mapx. This is **no meaningful end-to-end speedup**; the
benefit of this task is correct, diagnosable recovery with similar runtime. Raw
evidence: `docs/benchmarks/decompile-win-x64.json`. The new bounds hierarchy avoids
incorrect candidate pruning; broader performance claims need other workloads.

## 2026-09-29 — Persistent CPU jobs and profiles

Replaced fixed per-pass thread arrays and mutex-per-item dispatch with persistent
workers, caller participation, atomic range claims, adaptive batches, independent
compiler locks, nested-job handling, and worker-exception propagation. Added
strict worker/subdivision/surface-limit parsing, `-threads auto`, named pass
profiles via `-profile`, and acquire/release publication of VIS portal results.

Validation: release build and 8/8 CTest groups pass. The job suite covers exactly
once execution, empty/small/large ranges, four grain sizes, 1/2/4/16/70 workers,
nested calls, concurrent submitters, failure propagation and pool reuse. Actual
VIS data matches byte-for-byte at 1, 4 and 70 workers. Invalid numeric arguments
fail cleanly; profile schemas and values are checked through the CLI.

On the same i7-13700H, a 20-worker microbenchmark of eight 262,144-item trivial
passes measured medians of 0.114084 s for the old scheduling algorithm and
0.0022981 s for the pool (about 49.6x less dispatch time). This is a synthetic
scheduler measurement, **not total compilation speedup**. Raw observations are in
`docs/benchmarks/scheduler-win-x64.json`; full-stage measurements are in progress.

Additional inherited issues found: unchecked large-file offsets, nontransactional
BSP replacement, unbounded diagnostic formatting, and potential uninitialized
`ClipWinding::bestNormal` moves. These remain separate follow-up tasks.

Full-stage follow-up (`docs/benchmarks/jobs-win-x64.json`) shows no broad compiler
speedup yet. At 20 workers, VIS was 0.9312 s, lighting 0.0606 s and minimap 0.0426 s;
at one worker VIS increased to 34.3020 s versus the earlier 26.8239 s baseline.
These sequential runs include system/run-order effects and the VIS race fix;
an alternating comparison is needed before attributing the change. The M3
algorithm-optimization item remains open. Lower scheduling overhead alone does
not accelerate this geometry-heavy fixture.

## 2026-09-29 — Safer file replacement and diagnostics

BSP and `SaveFile` outputs now reserve unique sibling temporary files and replace
the destination only after a successful write/close. Replacement errors are
reported and preserve the original. Interrupted C++ operations clean temporary
files; legacy fatal `Error()` exits can leave a recoverable `.q3mapx-*.tmp` sibling.
This protects against ordinary write/replacement failure; it is not a promise of
power-loss durability on every filesystem.

Added checked 64-bit file-length queries with an explicit current 2 GiB format
limit, checked BSP lump sizes/offsets and seeks, checked output closes, bounded
absolute paths and diagnostic formatting, serialized console/XML initialization,
safe literal diagnostic strings, public libxml buffer APIs, and initialized model
clipping normals. These address the issues recorded in the previous task.

Validation: release build and 9/9 CTest groups pass. Atomic-file tests cover
successful replacement, interrupted writes, independent temporary names, and a
failed replacement that preserves existing contents. Compiler round trips and
corrupt-BSP checks continue to pass. Legacy paths outside BSP/`SaveFile`, including
some direct text exporters and asset decoders, still need broader hardening.

## 2026-09-29 — Indexed CPU minimaps

Replaced the per-sample full brush scan with compact spatial candidate lists and
contiguous plane data. Removed assumptions about brush-side ordering and made
random sampling deterministic across workers. Added a reference backend, random
seed option, strict minimap numeric bounds, finite nonempty extents, and buffer
cleanup. The same data layout is suitable for the upcoming GPU implementation.

Validation: release build and 11/11 CTest groups pass. Fifty thousand randomized
column queries, brush boundaries, shuffled planes and nonaxial brushes match the
exhaustive reference. Real minimap images match at 1/4 workers and across reference
and indexed paths, including random samples and postprocessing. Invalid arguments
fail cleanly.

Alternating five-run end-to-end benchmark on the 968-brush, 2048², four-sample
fixture: NRC median 2.5647 s, indexed CPU 0.0862 s, **29.8x speedup with exact image
parity**. Evidence: `docs/benchmarks/minimap-cpu-win-x64.json`. This is a minimap
speedup; it does not imply faster BSP construction or lighting.

## 2026-09-29 — Optional OpenCL compute

Implemented real GPU column sampling with dynamic loading, device enumeration,
explicit/automatic backend selection, bounded row dispatches, checked resource
operations, RAII cleanup, finite-result checks and setup/transfer/kernel reports.
Embedded first-party kernels need no runtime source files. Vendored only the
three required Khronos Apache-2.0 headers after checking compatibility/attribution.
Added `-devices`, `--version`, `-gpu-device`, `-compute-report`, and a CPU-only preset.

Validation: 12/12 CTest groups pass in both GPU-enabled and GPU-disabled builds.
Actual NVIDIA RTX 4060 Laptop and Intel Iris Xe execution passed image parity
(maximum difference one grayscale level), including randomized samples and
postprocessing. Missing-driver behavior, required-GPU failures and automatic CPU
fallback passed. Cold compilation, small-workload overhead and platform limits
are explicitly documented in `PERFORMANCE.md`.

Final alternating whole-process comparisons: 4096²/16 samples CPU 0.7364 s versus
GPU 0.3808 s (1.93x); 2048²/four samples CPU 0.0924 s versus GPU 0.2496 s. Automatic
selection keeps the small case on CPU. Reports include the exact measured binary
hash. Lighting was profiled and its material semantics evaluated; it remains on
CPU, with the rationale and future parity requirements in `GPU-LIGHTING.md`.

## 2026-09-29 — Native Qt workbench

Added a separate Qt 6 desktop application with versioned project files, isolated
run folders, three quality presets, exact argument previews, CPU/minimap backend
settings, six workflows, asynchronous queues, dependent-stage skipping, graceful
cancel with bounded kill fallback, persistent full logs, searchable live output,
diagnostics, JSON report viewing/export, device discovery, history/project reload,
light/dark themes, label mnemonics and keyboard shortcuts. The CLI remains separate;
Qt is optional through `Q3MAPX_BUILD_GUI`.

Validation: the preceding 12 compiler groups plus the new Qt queue integration
test pass. The queue performs real BSP/VIS/LIGHT, decompilation and minimap runs;
checks spaced paths, project round trips, invalid fields, untouched source files,
failed starts, dependent skips and cancellation. A separate offscreen preview test
also passes (14 registered groups total). The actual Qt-painted interface was
inspected from `build/release/tests/workbench/workbench.png`; no OS capture or
mouse/keyboard injection was used. Preview logs/settings stay in that test folder.

Known limits: wider real-game and assistive-technology testing is outstanding;
lighting remains CPU based. Build history is bounded to 100 runs and the live log
view is bounded, while full logs remain in run folders. Runtime packaging follows.

## 2026-09-29 — Numeric and portal input boundaries

Replaced direct `atoi`/`atof` consumption in BSP, lighting, conversion and packing
options with complete checked numeric parsing. Added bounded supersampling,
lightmap dimensions/search shifts/sample scales, byte-safe raw lightmap allocation
sizes, and an explicit 1,000-byte argument limit for inherited fixed path buffers.

Portal input now rejects negative/oversized counts, off-by-one and negative leaf
indices, invalid face/point counts, non-finite vertices and degenerate planes.
It verifies that portal clusters cover BSP leaves. A further inherited defect was
found: 131,072 file portals were accepted despite scratch space for only 131,072
directed portal bits. Input is now bounded to 65,536 file portals until scratch
storage is expanded safely; malformed counts fail before allocation/traversal.
Clipping scratch arrays now cover the full accepted 512-point winding plus its
closing sentinel, replacing undersized 128-entry arrays.

Validation: the expanded release suite has 15 groups. New portal regressions
exercise 30 strict/`-force` failures and verify that the original BSP is preserved.
129- and 512-point portal inputs complete or report the inherited separator-cache
limit safely; that geometric complexity limit remains explicit.
Lighting option tests cover huge samples/dimensions, invalid shifts, non-finite
values and oversized combined supersampling. Existing compiler/GUI paths pass.
This hardens compiler-owned inputs; third-party image/model decoders still need
broader sanitizer/fuzz coverage.

## 2026-09-29 — Visibility working sets and bitsets

Replaced platform-sized `long` visibility intersections with aligned 64-bit words,
fused redundant copy/intersection loops, moved recursive scratch frames to reusable
worker-local heap storage with a 1,024-depth diagnostic, and made initial portal
reachability iterative with one visit per leaf. Small clipping windings use compact
stack arrays; large input windings use reusable thread-local scratch. Removed
undefined subtraction of winding pointers belonging to different allocations.

Validation: all four VIS algorithms match captured NRC visibility hashes on the
grid=5 fixture. The alternating grid=9 benchmark measures 2.6156 s NRC versus
2.2991 s q3mapx with one worker and exact output parity (12.1% less elapsed time).
The preceding q3mapx build took 2.6636 s. Full raw evidence is in
`docs/benchmarks/vis-win-x64.json`.

Additional inherited issue: default multithreaded VIS can vary by one or two bits
on grid=9, including in NRC itself. The raw results retain these differences rather
than labeling them parity. An explicit reproducible scheduling mode follows.

## 2026-09-29 — Reproducible parallel visibility

Added `-vis -reproducible` with stable portal tie-breaking and a fixed 64-job
publication frontier. A job can reuse only fully completed preceding batches,
removing the inherited dependence on when another worker finishes a portal.
The workbench exposes the setting and enables it for new projects; older JSON
projects preserve their prior behavior. Legacy CLI scheduling remains available.

Validation: on the grid=9 fixture that exposed the issue, all three precise VIS
algorithms produce identical bytes at 1, 4, 20 and 70 workers, including a repeated
20-worker run. This is repeatability for the same input/build, not a cross-platform
floating-point guarantee. Batch barriers can cost performance. The Qt project
round-trip/queue test verifies the setting and backward-compatible loading.

## 2026-09-29 — Portable Windows packaging

Added a project-local packager using CMake install, Qt deployment and a recursive
PE dependency audit. It records 38 runtime DLLs from 24 installed packages, copies
their license notices, records binary/source hashes, embeds the project source,
and fetches exact versioned dependency sources plus build recipes. The source
lookup handles both current zstd and older gzip archives. Windows system DLLs and
GPU drivers stay external. The package uses relative Qt plugin lookup and does
not change system settings. Installation and rebuild steps are in `RELEASE.md`.

Validation: the staging package passed actual BSP/VIS/LIGHT, all MAP round trips,
decompilation recovery, CPU/GPU minimap checks and the Qt queue with PATH restricted
to the package and Windows directories. A direct Qt-painted preview was inspected;
plugin diagnostics confirm the packaged offscreen plugin was loaded. File hashes
were checked before running. The final clean-source archive follows the integration
commit. All 24 corresponding dependency source archives are retained under
`build/package/dependency-sources` (some packages share one source archive).

## 2026-09-29 — Build matrix and final integration

Validated Windows release with GPU support and workbench (16/16 CTest groups),
GPU-disabled workbench build (16/16), and Qt-free CLI build (14/14). Portable runtime
smoke checks also pass with only package/system DLL paths available. The test
matrix includes corrupt inputs, actual compile/recover pipelines, original NRC
VIS hashes, scheduling, reproducible VIS, atomic files, CPU/GPU output parity and
asynchronous GUI workflows.

Added named CLI-only and Linux sanitizer presets and expanded CI matrices. ASan/
UBSan instrumentation now covers the static support libraries and regression
targets, not just the main executable. The sanitizer preset uses the smaller
grid=5 VIS stress fixture and disables legacy process-lifetime leak reporting;
address and undefined-behavior diagnostics remain fatal. Linux and sanitizer CI
have not been executed from this Windows environment, and MSVC/macOS remain
unvalidated. Broader real-map, decoder fuzzing and manual accessibility work are
recorded as limitations rather than implied by the synthetic suite.

## 2026-09-29 — Release documentation and cleanup status

Updated the README and architecture to describe implemented functionality, linked
the installation guide, and completed the integration/packaging entries in the
plan. The portable delivery is generated from this clean committed source under
`build/package/q3mapx-0.1.0-windows-x64`; package-validation logs and the Qt preview
are kept in `build/package-validation`. Benchmark evidence stays in
`docs/benchmarks` and its reproducible harnesses in `benchmarks`.

Cleanup limitation: exact staging roots were verified to contain no reparse links.
Automatic approval review nevertheless rejected recursive cleanup, a safer
file-by-file cleanup, and deletion of the single explicitly named disposable
`q3mapx-package-test/qt-deployment.log`. The only stated reason was "blocked by
policy". No alternate deletion mechanism was used. Failed/successful staging copies
remain under `build/package/q3mapx-package-test*`, and temporary import/download
helpers and useful build logs remain in `.agents/tmp/`. That plan item stays open.

Unrelated inherited issues/limits remain visible in `RELEASE.md`: non-standard-layout
`offsetof` warnings, broader third-party decoder fuzz coverage, legacy fatal-exit
profile/temp-file behavior, and missing Linux/sanitizer/manual-accessibility runs.
GPU lighting is an explicitly documented future parity gate; minimap GPU work is
implemented and tested on both available native GPUs. All changes are committed
locally; no remote push or publication has been performed.

## 2026-09-29 — Linux validation and material-aware lighting fixes

Used Ubuntu 24.04.3 under WSL with GCC 13.3 and Qt 6.4.2. Missing Assimp,
Draco, PugiXML and Minizip packages were extracted beneath the project, without
changing the system installation. Corrected a designated aggregate initializer
that GCC 13 rejected. Windows release and Linux release now pass 17 CTest groups;
Linux ASan/UBSan passes 15 (GUI/GPU disabled by the sanitizer preset). Linux has
no usable OpenCL GPU, so its hardware parity cases explicitly skip; Windows
continues to exercise the native NVIDIA and Intel drivers. Build/test logs remain
under `.agents/tmp/linux-deps`; Linux test artifacts are in `build/linux-release`
and `build/linux-sanitized`. These are local runs, not remote CI results.

Added an original material fixture covering alpha shadows, colored transmission,
sun/sky, emitters, a patch and a brush-model origin. Tests check adaptive sampling,
bounces, deluxemaps and ordered supersampling at one and four workers. Independent
alpha/RGB image mutations and a no-trace comparison prove those paths affect the
result. This exposed an adaptive-sampling null deluxel reference under UBSan and
an incorrect reuse of the first subsample's visibility cluster. Both are fixed.

Repeated runs also exposed allocator-dependent shader-pointer sorting. Lightmap
setup and packing now use shader names and stable surface/lightmap indices, and
setup records the actual BSP model index. Twenty repeated Windows material tests
pass with exact lighting-lump parity. Corrected sampling and packing can change
older outputs intentionally. The original portable archive predates these fixes;
the final package will be refreshed after the remaining lighting work.

Unrelated inherited limits: `offsetof` and non-trivial-object `memset` warnings
remain; ASan/UBSan does not certify thread-race freedom or third-party decoder
fuzz coverage. Shared lighting statistics and bounce-light publication order are
next audit targets. The previously reported cleanup policy block remains.

## 2026-09-29 — Deterministic lighting jobs and working floodlight sampling

The dense benchmark found schedule-dependent lightgrid bytes even when comparing
the same executable. Grid escape nudges, randomized dirt and adaptive samples now
use independent, repeatable streams seeded by pass and work item. Diffuse lights
are built into per-surface lists and published in serial surface order after the
job barrier, eliminating allocator locks and scheduling-dependent accumulation
order. Worker culling counts are reduced in batches; other shared lighting
statistics use relaxed atomic updates and 64-bit storage.

Grid tracing now reuses bounded worker storage instead of probing a 1.25 MiB stack
array for each point, and reserves both optional floodlight contributions. The
inherited low-quality floodlight branch never traced a ray; it now uses eight
stratified samples. Dirt/flood traces initialize their traversal mode rather than
using an uninitialized value or the previous sunlight trace's state. These fixes
intentionally change old randomized/low-quality/affected vertex-lighting output.

Validation: Windows release passes 17/17 groups. Expanded material tests and the
compiler pipeline/job pool pass Linux ASan/UBSan (3 targeted groups). Dense
randomized bakes, including bounced grid light, compare exact lightmaps, vertex
colors, grid bytes and diagnostic counters at 1, 4, 20 and 70 workers. Alpha/RGB
filter and low-quality floodlight checks assert observable output changes.

The alternating grid=21 surface-lighting benchmark (five runs after a warmup,
grid lighting disabled to isolate the intentional RNG change) retained exact
surface/vertex/lightmap output. Median time changed from 2.4878 to 2.5445 s at
one worker and 1.1452 to 1.1871 s at 20 workers. This is a correctness improvement,
not a speedup claim; the 2–4% cost is recorded in
`benchmarks/light-jobs-win-x64.json`. The main bounced-light pass remains the
performance target. The executable was hash-checked before and after measurement.

Unrelated issue observed: GCC also reports a possible nonzero-offset deallocation
in inherited `StringBuffer` code; it has not triggered these sanitizer tests and
needs a separate focused audit. Existing `offsetof` warnings and cleanup policy
limitations remain. No claim of comprehensive thread-sanitizer coverage is made.

## 2026-09-29 — Conservative lighting culling and polygon scratch repair

Mapped lightmap samples now have conservative 8x8 tile bounds. Initial samples
outside a light's envelope avoid its per-sample work. Bounds use actual nudged
positions, account for float rounding and retain uncertain/non-finite tiles.
Adaptive refinement still traverses the entire map: in-place refinement can
propagate outside the initially lit region. Added `-no-light-culling` for direct
output/performance comparisons. Common polygon lights use a small local buffer;
large polygons use dynamic storage with room for the closing vertex, fixing the
inherited out-of-bounds write at 512 points without changing the math.

Validation: Windows release 18/18 groups; Linux ASan/UBSan compiler pipeline,
material lighting and math 3/3 groups. The math regression covers degenerate and
reversed polygons and sizes through 1,024 vertices. Randomized spatial checks
include large coordinates and envelope boundaries. Each material mode compares
culling enabled/disabled along with varied worker counts.

The complete grid=21 lighting benchmark with grid, four adaptive samples and one
bounce retains exact vertex/surface/lightmap/lightgrid bytes across every run.
Five alternating runs after warmup measured 2.5814 → 2.2606 s at one worker (12.4%
less time) and 1.1659 → 0.9062 s at 20 workers (22.3% less time), against `dda68d7`.
Executable hashes and all observations are in `benchmarks/light-culling-win-x64.json`.
Useful local artifacts remain in `build/lighting-culling-final`; sanitizer logs
are `.agents/tmp/linux-deps/{build,test}-lighting-culling.log`.

No new unrelated issue was found. Previously recorded inherited compiler
warnings, broader fuzz/manual-validation gaps and the cleanup policy block remain.

## 2026-09-29 — Batched GPU area-light evaluation and CPU-default decision

Implemented optional OpenCL polygon-light integrals with matching mixed-precision
math, reusable context/program ownership, bounded dispatches and streamed caches.
The four largest eligible lightmaps may use at most 64 MiB of result storage each;
larger light sets stream in groups. The existing CPU code retains material tracing,
alpha/colored transmission, sun/point/spot lighting, adaptive samples, vertices
and lightgrid. `-light -light-backend gpu` is explicitly experimental;
`-compute-report` distinguishes actual hybrid work from CPU/fallback execution.
Startup failure is visible before baking, and failed batches clear cached offsets
before CPU fallback. The Intel driver lacks the required FP64; NVIDIA is tested.

Validation: Windows release 21 passed; Windows CPU-only and Linux release each
20 passed plus one hardware skip; Linux ASan/UBSan 18 passed plus one hardware
skip. Linux has no usable GPU and its bake parity cases explicitly skip after
checking failure behavior. NVIDIA unit math matched exactly through 1,024-point
windings; complete material bakes match at one/four workers, including full and
approximate lighting modes. The dense 20-worker case streams 44,211,000 factors
in ten batches with exact CPU output. Qt queue/preview checks pass; the native
offscreen widget preview was inspected without input injection or OS capture.

Benchmarking does not justify changing the default. Fast grid=21 medians (five
alternating measured runs) were CPU/hybrid 2.2287/2.5518 s at one worker and
1.0780/1.4624 s at 20 workers. Full-envelope grid=9 (three runs) measured
18.4584/18.3714 s and 1.2532/1.8160 s, respectively. The tiny single-worker
full-envelope difference is within the observed spread. Cold/warmup and setup
costs are material. Raw profiles, all observations and parity evidence are in
`benchmarks/light-gpu-{fast,accurate,parity}-win-x64.json`; see `GPU-LIGHTING.md`.
No lighting GPU acceleration claim is made. Automatic minimap GPU selection and
the measured CPU lighting optimization remain the recommended paths.

A follow-up caught overhead from cache checks in the ordinary CPU path. CPU/GPU
inner loops are now separate compile-time specializations. Five alternating CPU
runs against the pre-GPU build measured 2.2600/2.2061 s at one worker and
0.9222/0.8896 s at 20 workers, with exact output and no observed regression.
See `benchmarks/light-gpu-cpu-dispatch-win-x64.json`. Allocation fallback also
releases optional host caches before continuing CPU work. Lighting checks were
repeated after these changes. Release metadata is advanced to 0.2.0; the portable
smoke harness now includes material and hybrid lighting checks.

The first full-envelope grid=21 trial was stopped during its long initial
single-worker warmup and replaced with grid=9; it is not used as timing evidence.
Useful local outputs are under `build/lighting-gpu-*`; platform logs remain in
`.agents/tmp/linux-deps`. No new unrelated defects were found. Existing inherited
warnings, broader real-map/fuzz/manual-accessibility gaps and cleanup policy limits
remain documented.

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

## 2026-09-29 — Validated 0.2.0 portable delivery

Built `build/package/q3mapx-0.2.0-windows-x64.zip` from clean source revision
`2be749b9cc2b3375caf366d1940cf1c6f9e100cd`. It is 39,817,147 bytes with 168 entries;
SHA-256 is `f2246e0eec4c9504d3ddfd830695ae6ce6d59f59395441e87a0d9972723cfe98`.
The package contains CLI/workbench executables, 38 dependency DLLs from 24 runtime
packages, license notices and the matching project source. All 23 unique dependency
source archives were hash-verified; archive CRC verification also passed.

The actual portable binaries passed the compile/recovery pipeline, CPU/GPU
minimaps, material lighting, hybrid lighting, Qt queue integration and direct
widget rendering with development DLL directories removed from PATH. The native
preview was inspected. Evidence is in `build/package-validation-0.2.0` and
`releases/0.2.0-windows-x64.json`. The 0.2.0 Windows lighting/GUI checks and Linux
ASan/UBSan pipeline/lighting checks were repeated after isolating CPU/GPU dispatch.
The final Linux release also passed eight targeted pipeline/lighting/GUI groups;
its hardware area-factor group explicitly skipped because no GPU is available.

The requested implementation areas and M7 release work are delivered. The separate
M6 cleanup item remains blocked by the earlier automatic approval rejections;
disposable staging copies remain under the documented project-local directories.
No remote publication or push was performed. Unrelated inherited `offsetof`,
non-trivial-object `memset` and possible `StringBuffer` deallocation warnings remain
documented, along with MSVC/macOS, wider decoder/real-map and manual accessibility
validation limits. The current material/sanitizer suites did not reproduce the
possible `StringBuffer` issue.

## 2026-09-29 — Indexed Raven lightgrid packing

Started the requested continuation on `codex/game-coverage`, with fnTech3 pinned
as an observation reference in `GAME-COVERAGE.md`. Replaced the quadratic Raven
lightgrid dictionary scan with a bounded index selected from independent lighting
components. It retains NRC's first acceptable entry, all style/color comparisons,
and the original 255-period direction seam. Grids above the format's reference
limit or the 65,535-entry dictionary fail before opening the temporary BSP.

Windows release checks passed for the compiler pipeline, malformed BSPs, grid
oracle and actual CLI serialization (4/4). Linux ASan/UBSan grid and CLI tests
passed (2/2), using the documented preset's disabled process-lifetime leak check.
An initial direct CTest invocation omitted that setting and reported 7,112 bytes
of inherited compiler allocations during fixture construction; the standalone
new packing test passes with leak detection enabled. No sanitizer suppressions
were added. Useful logs are `.agents/tmp/game-coverage/*lightgrid*.log`.

Whole-command `-scale 1` measurements retain all eighteen BSP lump payloads.
Jedi Academy's retail `maps/mp/duel9.bsp` (28,749 grid entries, 496,800 references)
measured 8.5414 -> 0.2705 s median in three alternating runs after warmup, 31.6x
faster. The high-variation generated fixture measured 1.7567 -> 0.1286 s over
five runs. These are serialization/rewrite measurements, not whole lighting-bake
claims. Reports with input/output/executable hashes are in
`benchmarks/lightgrid-{native,synthetic}-win-x64.json`. Proprietary input remains
only in ignored local test areas; it is not included in the repository or reports.

Related compatibility issue discovered: several retail Jedi maps contain NaNs in
unused extra lightmap-coordinate slots. The current validator rejects them even
though the referencing surface marks those slots unused. The successful native
benchmark uses an unmodified map with finite records; the other maps were not
silently sanitized for comparison. Fixing inactive-slot validation is the next
task. Existing inherited warnings and cleanup restrictions remain documented.

## 2026-09-29 — Native Raven unused-field compatibility

Retail-map validation exposed unused NaNs in both extra lightmap slots and
vertex-lit patches' primary slots. The loader now proves a coordinate unused
against every referencing surface before normalizing it. Used coordinates,
including shared vertices, still fail on non-finite values under strict and
`-force` modes. Sparse sorted candidate queries avoid a full vertex scan for
every overlapping surface. Eleven zero-geometry flare records in Jedi Outcast's
`yavin_temple` also refer to fog 0 despite an empty fog lump; those become no-fog
references. Geometry-bearing invalid fog references still fail. Diagnostics and
recovery JSON report both repairs.

Windows malformed-file/recovery/grid/native-record regressions passed (4/4);
Linux ASan/UBSan malformed-file/recovery/native-record checks passed (3/3, using
the documented leak-detector setting). The optional archive harness validates
61 Jedi Academy entries, including patched versions, and 41 Jedi Outcast entries:
102/102 pass. Of those entries, 73 contain unused non-finite lightmap coordinates.
Two real decompilations, `t3_stamp` and `yavin_temple`, recover 4,118/2,725 brushes
and 338/289 patches respectively, with no skipped brushes. Unrecoverable/hidden
brush faces still use reported fallback UVs; this does not promise the original
editor source or gameplay validation.

Source archives were read-only, compiler/input hashes were checked, and no retail
assets are committed. `validation/native-raven-win-x64.json` records the evidence;
local outputs are in `build/native-{validation,recovery}` and logs in
`.agents/tmp/game-coverage`. Format observations are credited to the pinned
fnTech3/OpenJK references in `GAME-COVERAGE.md`; no external implementation text
was copied. No new unrelated issue was found beyond the already documented
inherited process-lifetime allocations, compiler warnings and cleanup restriction.

## 2026-09-29 — Compiler profile catalog and complete workbench discovery

Added the schema-versioned `-games` catalog, readable profile titles, fnTech3-style
aliases and strict unknown-profile errors. The Qt workbench now discovers the
selected compiler's complete profile list, keeps saved choices, displays native
format/asset information and gates unsupported workflows before staging. Catalog
queries are asynchronous, bounded and timed; stale results cannot replace a newer
compiler's response. Older compilers retain manual selection with a diagnostic.

All nineteen native writers pass generated BSP/VIS/LIGHT, recovery/recompilation
and minimap pipelines on Windows release, Linux release and Linux ASan/UBSan
(the documented process-lifetime leak setting). Windows/Linux catalog, queue and
direct widget-render tests pass. A separate offscreen actual-window test starts
and completes a three-stage build without input injection; both platforms pass.
Windows evidence is `validation/game-profiles-win-x64.json`; detailed local logs
are in `.agents/tmp/game-coverage`. The rendered window was inspected.

The window test caught a Windows path-separator mismatch in the new discovery
guard; that is fixed. An unrelated stale GUI version string was corrected to the
build version. Version queries now terminate before GUI initialization, avoiding
Qt's modal Windows version dialog when output is not redirected; both inherited
and captured-output probes pass. Existing compiler warnings and cleanup limits
remain documented. These fixture checks do not establish retail gameplay support.

## 2026-09-29 — Bounded native BSP directory inspection

Added `-inspect` with human-readable and pure JSON output. It recognizes eleven
directory layouts across IBSP 43-47, RBSP, FBSP, FAKK and MOHAA's `2015` format,
checks ranges/record sizes/overlaps from at most 256 bytes, and explicitly reports
shared game signatures and alternative IBSP 47 directories. Geometry validity,
native reader availability and compilation support remain separate claims.

Windows inspection/malformed-BSP checks pass (2/2), as do Linux release inspection,
catalog, queue and actual-window checks (4/4). The initial sanitizer inspection
run reached nine layout groups before its 90-second test timeout; instrumented
CLI startup across the many subprocess cases needs a larger suite timeout. It
was raised to 240 seconds without relaxing per-command timeouts or assertions.
The complete ASan/UBSan rerun passed in 70.3 seconds after the concurrent build
load ended; no sanitizer error was reported.

Read-only directory probes pass for 36 Alice, 30 F.A.K.K.2 and 54 Allied Assault
archive entries (120/120). Input and executable hashes plus results are in
`validation/native-inspection-win-x64.json`; local logs are under
`.agents/tmp/game-coverage`, with staged input in `build/native-inspection`.
No proprietary bytes or external implementation text are committed. The audit
corrected the initial MOHAA signature in GAME-COVERAGE.md from FAKK to `2015`.

Related legacy issue discovered during the audit: the inherited `-analyze` path
reads its guessed directory/payload before checking file ranges. The new inspector
does not call it. Hardening that retained CLI path is the next robustness task.

## 2026-09-29 — Safe legacy heuristic BSP analysis

The retained `-analyze` command now bounds guessed directory entries and payload
ranges before access, including signed/overflowing fields under `-force` and
`-lumpswap`. Unaligned scalar reads use copies, payloads shorter than four bytes
are not read as scalars, and string probes terminate at their actual copied length.
It retains the heuristic unknown-format behavior; `-inspect` provides known
native-directory interpretation.

Generated tests pass on Windows release and Linux ASan/UBSan (24.1 seconds for
the instrumented subprocess suite). The first Windows assertion exposed CRLF in
captured text; the harness now normalizes line endings before comparison. Local
logs are `.agents/tmp/game-coverage/*analyze*.log`. The issue found during the
inspection task is fixed; no additional unrelated defect was discovered.

## 2026-09-29 — Native Alice/F.A.K.K.2 recovery and FTX textures

Added recovery-only `alice` and `fakk2` profiles, direct native directory/record
adapters, mandatory MAP loss reports and retained shader/subdivision metadata.
Unsupported compilation, modification and BSP conversion fail before stage
outputs are created. The workbench discovers both profiles and disables build
for them. Shared IBSP prefix conversion retains the existing native writers.

Actual recovery initially exposed missing texture lookups: both games primarily
use FTX. Added an independently written bounded RGBA reader. The generated
fixture uses unequal texture dimensions and verifies UVs after recovery and
Quake III recompilation, exposing missing-image fallback rather than merely
checking that export succeeded. Native strides, metadata, wrong versions,
malformed geometry, minimaps and output protection are also covered.

Windows native/profile/validation/recovery/catalog checks pass (6/6), followed
by catalog and actual-window build-gating checks (3/3 with the queue fixture).
Linux release native/inspection/window checks pass (5/5). Linux ASan/UBSan
validation, FTX and native recovery checks pass (3/3, 63.3 seconds), with the
documented process-lifetime leak setting.

All 36 Alice and 30 F.A.K.K.2 maps pass native geometry validation. Real MAP
recovery of `centipede1` and `towncenter_good` produces 1,149/2,768 brushes and
44/35 patches, zero skipped brushes and zero missing-texture warnings after FTX
support. Native baked-light extension loss and hidden-face UV fallbacks remain
explicit. Evidence is `validation/native-fakk-win-x64.json`; private outputs are
in `build/native-{validation,recovery}/{alice,fakk2}`, with logs under
`.agents/tmp/game-coverage`. No proprietary assets or external implementation
text are committed. Unknown native shader directives and the existing decoder,
compiler-warning and cleanup limits remain; full native writing/gameplay is not
claimed. No new unrelated defect was discovered.

## 2026-09-29 — Native Allied Assault terrain and placement recovery

Added the read-only `mohaa` profile with actual 2015/19 records, checked fence
equations, static-model placements and native terrain. Full-resolution OBJ/ASE
terrain respects both orientations and terminal hole bits. MAP JSON retains raw
terrain arrays and model placements; it explicitly identifies missing external
TIKI meshes and baked-light data. The catalog disables brush-only minimaps for
this profile. Resource limits bound terrain and expanded leaf references.

All 54 installed maps validate. Eight briefing/credits/void maps exposed zero
equation references without an equation table; only absence of every fence
contents flag permits normalization. Active invalid references remain fatal.
Real MAP/OBJ recovery of `mohdm3` and `m1l1` has no skipped brushes and retains
2,689/1,128 terrain triangles and 294/135 static-model placements. `mohdm4`'s
21,475 removed terrain triangles match the pinned fnTech3 observation. Evidence
is `validation/native-mohaa-win-x64.json`; private inputs/outputs stay under
`build/native-{validation,recovery}/mohaa`.

Windows terrain/native/catalog checks pass (4/4). Linux release terrain/native/
catalog/window checks pass (5/5 including the queue fixture), and Linux ASan/UBSan
terrain/native checks pass (2/2, 21.0 seconds, documented CLI leak setting).
Generated checks cover geometry insertion before brush submodels, all terminal
hole bits, malformed extensions, hard allocation limits and existing output
preservation. Logs are `.agents/tmp/game-coverage/*mohaa*.log`.

Known recovery limits include native shader warnings, the `textures/notexture`
missing-image fallback and hidden-face UV fallback. No game or input control was
used. No proprietary assets or external implementation text are committed. The
existing decoder/compiler-warning/cleanup limits remain; no new unrelated defect
was discovered.

## 2026-09-29 — Early Quake III formats and inferred brush materials

Added native IBSP 43/44/45 recovery profiles, direct record conversion, validated
head-node ownership, linear stable model ordering, and source metadata in JSON.
Versions 43/44 have no brush-side material names; the existing triangle indexes
now infer names for these profiles by positive coplanar overlap. Fast recovery
keeps explicit fallback faces. Other profiles retain material-constrained lookup.

All four available IHV maps and sixteen archive entries across six public test
releases pass native validation. Nine real MAP/OBJ recovery probes pass with no
skipped collision brushes. The 1.05 `q3test1` recovers 2,529 brushes/177 patches
and infers 6,167 face materials; its stale declared model range is preserved in
the report instead of trusted. Private outputs are under
`build/native-{validation,recovery}/{q3-ihv,q3test44,q3test45}`. Evidence is
`validation/native-early-win-x64.json`, with no proprietary bytes.

Generated tests exercise real differing strides, unaligned data, deliberately
reversed geometry, stale ranges, brush-entity preservation and texture-sensitive
MAP/Quake-III round trips. Malformed origins, graph cycles, invalid head nodes,
shared model ownership, strings, vertices and indices fail even with `-force`
and preserve existing MAP outputs. Windows native/catalog/recovery checks and
Linux release native/inspection/catalog/window checks pass (6/6, including the
queue fixture). Linux ASan/UBSan native/recovery checks pass (2/2, 43.1 seconds,
documented CLI leak setting). Logs are `.agents/tmp/game-coverage/*early*.log`.

Separate export defects discovered: OBJ/ASE omit Bezier patches, OBJ ignores
brush-entity origins, and ASE normal lookup ignores each surface's vertex base.
These are queued for a dedicated export repair. Early shader dialect warnings
and missing-image fallbacks remain explicit. Existing decoder, platform and
cleanup limits are unchanged; no game launch or input control was used.

## 2026-09-29 — Complete curved mesh export and workbench workflows

OBJ/ASE now include bounded quadratic patch tessellation, with configurable
1–32 samples per span (default 8). Independent rows use the persistent job pool
and fixed output ranges; one/four-worker exports are byte identical. Positions,
UVs/lightmap coordinates and colors interpolate the controls, with tangent
normals and finite degenerate fallbacks. Allocation limits are checked before
dispatch or output. MAP recovery retains its original patch controls.

Repaired OBJ entity origins and sibling MTL names, ASE world-space placement and
surface-local normals. Geometry checks also exposed MOHAA terrain's opposite
winding in the canonical BSP representation; it now faces the same direction as
its normals. External-lightmap lookup no longer subtracts past short tokens or
copies arbitrarily long shader names into a 256-byte array. OBJ emits only used
lightmap material IDs, avoiding work proportional to a hostile sparse index;
deluxemap integer overflow is rejected. Embedded lightmap counts use actual
loaded bytes. Shader lookup precedes opening mesh outputs.

The catalog and workbench expose OBJ and ASE workflows for every reader,
including recovery-only profiles. Project JSON saves mesh curve detail and
defaults missing older fields to 8. Queue tests run both real exports, and the
offscreen window test verifies capability gating, option preview and direct
widget painting. The Windows quality-controls image was inspected; no input
events or operating-system capture were used.

Windows mesh/native/GUI checks pass; a test-local name collision initially
shadowed the MOHAA fixture builder and was corrected. Linux release mesh/native/
GUI checks pass (5/5). Linux ASan/UBSan mesh/MOHAA checks pass (2/2, 30.7 seconds,
documented CLI leak setting). Geometry-sensitive tests check the exact curved
center at (-128,-128,52), texture coordinates, triangle orientation, surface
normals, translated door bounds, malformed limits and preservation of outputs.

Real exports now include the previously absent curves: `mohdm3` has 56,272 faces,
`m1l1` 113,222, `ihv_test1` 85,601 and `km_portal` 63,559, at default detail.
Evidence is `validation/mesh-export-win-x64.json`; private outputs are under
`build/native-mesh/`, and logs are `.agents/tmp/game-coverage/*mesh*.log`.
External TIKI meshes, native shader effects and interrupted-output transactions
remain limitations. The separately discovered export defects are fixed; no new
unrelated issue remains from this task.

## 2026-09-29 — Controlled fatal exits during parallel compilation

The full Linux release run exposed an intermittent segmentation fault after
multiple workers hit the inherited portal separator limit. A focused rerun
reproduced three crashes in twelve attempts: legacy `exit()` handlers destroyed
global compiler state while other workers were still using it. Fatal errors now
serialize one diagnostic, flush console/editor feedback, close that connection
and terminate with status 1 without invoking those handlers or static destructors.
Normal completion still joins workers and performs ordinary cleanup.

Portal tests now require status 1 for malformed inputs and repeat 129/512-point
windings with 1/4/16 workers, checking a single fatal diagnostic and unchanged BSP
contents on failure. Windows release passes all 35 tests (78.07 seconds); Linux
release passes 34 with the GPU area-factor test skipped (116.13 seconds). The
Linux ASan/UBSan portal check passes (40.60 seconds). The original broader
sanitizer run was stopped so it can be rerun against this repair. Evidence and
the pre-fix reproduction logs remain in `.agents/tmp/game-coverage/*fatal*` and
`build/linux-release/tests/portals/fatal-race-before-*.log`.

Fatal paths still omit CPU profiles and may leave incomplete non-transactional
outputs; the architecture guide states that limit. The compiler still warns
about truncating exceptionally long diagnostics into its fixed message buffer.
No separate new unrelated defect was discovered.

## 2026-09-29 — Prepare the 0.3.0 portable release

Bumped both applications to 0.3.0 and refreshed the usage, coverage, performance
and packaging documentation. The package name now follows the CMake version,
and packaging rejects a stale CLI or workbench version before creating its
output directory. Both current-version acceptance and stale-version rejection
were exercised against the real binaries. All thirteen Markdown guides have
valid local links.

Expanded portable verification to the nineteen native writers, all new recovery
families, curved meshes, inspection, Raven compatibility/packing and parallel
failure handling. It verifies the embedded source archive, reports each completed
workflow and records the compiler version. All 23 cached dependency source
archives match their previously validated hashes (452,131,078 bytes total).
Release and targeted sanitizer results are recorded above; full instrumented and
portable delivery results belong to the following artifact audit. No new
unrelated issue was discovered during release preparation.

## 2026-09-29 — Validated 0.3.0 delivery

Final integration passes: Windows release 35/35 (78.07 seconds), Linux release
34 passed with one hardware skip (116.13 seconds), and Linux ASan/UBSan 30 passed
with one hardware skip (673.19 seconds). The sanitizer configuration also builds
without Qt or GPU support. Linux lacks a suitable OpenCL device; the GPU scripts
still check unavailable-device rejection and CPU fallback. Instrumentation keeps
address/undefined-behavior checks enabled and uses the documented CLI leak setting.
The [integration matrix](validation/release-0.3.0.json) records configurations,
binary hashes, every test and the parallel-fatal-exit repair found during this run.

The portable Windows package passes all 18 groups with development DLL paths
removed, including all nineteen writer pipelines, six native recovery profiles,
curved exports, Raven packing, inspection, fatal failure handling, the Qt queue
and direct widget painting. Actual minimap parity executes on the NVIDIA RTX 4060
Laptop GPU and Intel Iris Xe; hybrid lighting parity also passes. The packaged
workbench image was inspected without input events or operating-system capture.

The archive is `build/package/q3mapx-0.3.0-windows-x64.zip`: 40,141,164 bytes,
180 members, clean source revision `95c37a655e6f9d9f8c4b056464f7ed1ab1286410`.
All 350 embedded source files match that checkout. Its 40 executable/DLL hashes
match the manifest; both executables match the tested Windows release. All 23
corresponding dependency source archives match their recorded hashes. The
[artifact audit](releases/0.3.0-windows-x64.json) records its SHA-256 and portable
results. This documentation audit follows the packaged source commit; no binary
or source archive has been altered, and no remote push or publication occurred.

Useful evidence remains under `.agents/tmp/game-coverage`,
`build/package-validation-0.3.0` and the private `build/native-*` directories;
dependency sources remain beside the archive. The earlier M6 staging-cleanup
denial is unchanged and was not retried or bypassed. No new unrelated defect was
found during delivery. Inherited compiler warnings, third-party decoder fuzzing,
manual accessibility, MSVC/macOS and game-runtime validation remain open limits.
New game profiles are recovery-only, and GPU lighting remains experimental with
CPU as its default; the coverage and performance guides state those boundaries.

## 2026-09-30 — Continuing development audit

The current checkout is clean at `ce4b773`; 0.3.0 remains the verified delivery.
Inspection confirms that OBJ and ASE still truncate destination files before
generation and ignore `fprintf`/`fclose` failures. OBJ also opens its material
companion after truncating the mesh, so a companion-open failure can destroy a
previous export. This is the first repair in the new continuation plan, followed
by workbench inspection and measurement of larger recovery workloads. The earlier
cleanup restriction is unchanged; no blocked deletion was retried.

## 2026-09-30 — Checked mesh output and paired-file rollback

OBJ/MTL and ASE now use checked stdio streams staged beside their destinations.
All buffered streams must close successfully before any destination is replaced.
OBJ publishes MTL before the mesh, keeping only the smaller companion as a rollback
copy. A reported mesh replacement failure restores that MTL, or removes a newly
created one. The helper rejects duplicate paths, directories and links; abandoned
streams and successful rollback copies are cleaned up. A failed restoration keeps
its original backup and names it in the error. Single ASE publication needs no
backup copy. Stdio uses bounded 64 KiB buffers for the many small text records.

The new real-command regression reproduces data loss in the packaged 0.3.0 writer:
an unwritable companion path empties the previous OBJ. The repaired writer preserves
it. Windows sharing locks exercise rollback with both existing/new MTL files and
successful retry. Linux process-local file-size limits force actual buffered write
failures for OBJ and ASE. Link tests protect source files. Successful OBJ, MTL and
ASE bytes match 0.3.0 on the same fixture; geometry and worker parity checks remain
green. No game, input injection or OS screen capture is used.

Windows release and Linux release pass seven relevant groups (6.82/13.44 seconds);
Linux ASan/UBSan passes four (44.41 seconds, documented CLI leak setting).
[Validation evidence](validation/export-outputs.json) records the checks and byte
comparison. Logs and the pre-fix reproduction remain under
`.agents/tmp/continuation`; generated test outputs are in each build's
`tests/export_outputs`. The shipped 0.3.0 archive is unchanged.

Several names cannot be published as one atomic filesystem operation: interruption
between the two OBJ replacements can leave mixed generations. Metadata and
power-loss durability are outside this guarantee; concurrent writers are unsupported.
These limits are explicit in the recovery guide. No new unrelated defect was found.

## 2026-09-30 — Workbench BSP inspection

Added a dedicated inspection page with an independent source field, optional
project-profile check, candidate games, selectable native directory layouts,
section sizes/offsets/record counts, diagnostic notes and JSON export. The table
and notes share a resizable divider, including at the 1024×720 minimum window
size. Invalid directories remain inspectable and exportable. Saving refuses to
replace the source BSP and uses the existing atomic JSON writer.

The QtCore inspector runs real CLI queries asynchronously, limits combined output
to 1 MiB, validates the schema and source/status consistency, supports cancellation,
and rejects stale replies. Its default deadline is ten seconds. Directory validity
and game ambiguity are explicit; the page does not imply geometry validation.

Windows and Linux release each pass six relevant groups (6.09/17.47 seconds).
The new model tests exercise valid/malformed native reports, schema mutations,
oversized stdout/stderr, timeout, cancellation, supersession and failed starts.
The actual window test builds a map, inspects it, changes layouts, exports JSON
and checks source protection. Direct widget paintings at 1380×920 and 1024×720
were reviewed without input injection or OS capture. A fresh-settings run exposed
and repaired a test setup omission: the render-output directory must exist before
the first capture. [Evidence](validation/workbench-inspection.json) records the
binary identities, checks and retained log/image locations. The 0.3.0 archive
remains unchanged.

Separate code review found that the older hardware-discovery query still reads
unbounded merged output on completion and has no generation check for superseded
replies. This needs a follow-up robustness task; the new inspector already has
both controls. Larger recovery profiling remains the next performance task.

## 2026-09-30 — Preserve recovered MAP/report pairs

Investigation of larger recovery workloads exposed a separate publication defect:
the previous build replaced an existing MAP before attempting its report. A real
directory-at-report-path regression confirms that failure replaced the original
editable map. MAP recovery now uses the checked output group already used by mesh
exports. Both streams finish before publication; report replacement precedes MAP
replacement, with rollback if the latter fails. Legacy conversion without a report
still works. The textual geometry and recovery schema are unchanged.

New regressions cover all three MAP encodings, implicit/explicit/absent reports,
directory destinations, Windows sharing-lock rollback and retry, Linux file-size
limits on MAP/report writes and linked-output rejection. Windows/Linux release
each pass thirteen relevant groups (8.89/32.65 seconds); Linux ASan/UBSan passes
seven (140.06 seconds, documented CLI leak setting). The window queue and native
Alice/F.A.K.K.2, MOHAA and early Quake III fixtures remain green.

Added a whole-command recovery comparison harness with alternating samples,
input/executable identities and exact MAP/report checks across worker counts.
The 47×47 generated room contains 2,216 brushes and 13,296 recovered faces. Output
is identical at 1/4/20 requested workers; measured variation does not demonstrate
a speedup from buffered staging. Reconstruction is still serial. The performance
guide records that finding, so further scheduling changes require finer profiling.

Private native comparisons retain identical MAP/report output on Jedi Academy
`t3_stamp` and Allied Assault `m1l1` at 1/4/20 workers. Asset loading and file
publication are included; their timing variation also does not establish a
consistent speedup. [Validation evidence](validation/recovery-outputs.json) links
the measured observations and records all three tested compiler identities.
Logs and the pre-fix reproduction remain under `.agents/tmp/continuation`; private
outputs are under `build/recovery-output-*`. No proprietary geometry is committed.

The recovery guide states the same multi-file interruption, concurrent-writer and
metadata limitations as mesh publication. No new unrelated issue was found in
these checks; the separately logged hardware-query hardening remains queued.
The previously packaged 0.3.0 archive is unchanged.

## 2026-09-30 — Expand recovery fidelity and intelligent compiler roadmap

Added M8–M10 to the implementation plan and two detailed designs:
[recovery inference](RECOVERY-INFERENCE.md) and
[compiler optimization](COMPILER-OPTIMIZATION.md). M8 covers detail/structural
classification from BSP/VIS/portal evidence, `func_group` reconstruction, stripped
entity-light inference with surface/sky/indirect contributions, spotlight target
matching/replacement, and additional UV/brush/patch/entity reconstruction tools.
It includes provenance, uncertainty, saved corrections and rebuild comparisons.

M9 is a separate VIS/portal workstream: diagnose regional inefficiency, conservatively
simplify supported VIS graphs, and develop coordinated full-build changes for poor
detail usage. M10 separately targets reduced rendered triangle counts while retaining
coverage, attribute interpolation, material effects, collision and visibility.
Both define trial changes, rejection/fallback, regional exclusions and measured
acceptance, including runtime visibility cost and difficult shader/LOD cases.

Source review confirms that BSP compilation creates the PRT before VIS runs,
that current detail recovery is a leaf-membership heuristic, that group compile
parameters are applied before `func_group` collapse, and that spotlight target
distance affects cone interpretation. The designs account for these existing
contracts. Primary inverse-rendering and appearance-preservation references are
linked as conceptual background; no external code or new dependencies were added.

Validation is documentation-only: reviewed the designs against the existing code,
checked local documentation links and whitespace, and kept every new implementation
item unchecked. Binaries and release artifacts are unchanged. No new unrelated
issue was found; the existing hardware-query and recovery-performance tasks remain
open alongside these additions. The continuing development goal remains active.

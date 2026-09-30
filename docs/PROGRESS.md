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

## 2026-09-30 — Shared BSP evidence and regional investigation

Implemented [`-bsp-evidence`](BSP-EVIDENCE.md), with separate reusable analysis and
CLI/report publication layers. Full native validation and SHA-256 provenance
precede observations of brush/model ownership, retained contents, exact unoriented
partition-plane associations and local leaf-path associations. Iterative traversal
with active ancestor planes excludes unrelated regional cuts. Shared acyclic node
graphs explicitly disable local path/subtree evidence instead of expanding all
paths. Early native head nodes and brush source indices are retained.

Regional summaries rank node subtrees by subdivision count and report leaf,
brush/surface, indexed-triangle and patch-reference costs. Stored PVS statistics
handle missing tables, row padding, unused tail bits and missing self bits.
Neither plane matches nor subdivision counts are presented as proof of original
detail flags or safe optimization. Inference, portal adjacency, authoring review
and automatic compiler transformations remain open in M8–M10.

Known-source paired fixtures compile 25 identical pillars as structural or detail.
The resulting trees have 159 versus 19 nodes and 84 versus 4 clusters, with 150
versus 25 local matching sides. The remaining detail matches are the common floor,
demonstrating a classification ambiguity. A separate spatial control distinguishes
a globally matching plane in another subtree from a local brush-path match.

Reports have fixed record/expanded-side/output ceilings and a configurable work
budget. The 64 MiB JSON limit is exercised with a compact generated BSP that would
produce a much larger report; checked staging preserves old output. Other checks
cover exact 1/4-worker report parity, malformed references, shared graphs/cycles,
overlapping model spans, six recovery-only profiles, nonzero native world roots,
input aliases and real buffered-write failures. Native probes additionally cover
eight private maps across Alice, F.A.K.K.2, Allied Assault, Raven and early Quake
III. Full geometry reports stay under `build/native-evidence`; only hashes/counts
are included in the [validation record](validation/bsp-evidence.json).

Windows/Linux release each pass thirteen relevant regression groups
(11.99/62.44 seconds); Linux ASan/UBSan passes seven (164.03 seconds, with the
existing documented CLI leak setting). The final expanded evidence test, including
the real streaming ceiling, passes on all three builds (1.35/5.99/33.60 seconds).
Verified 118 local links across sixteen documentation guides and retained binary
identities in the validation record. This is functional evidence, not a speedup
claim or proof that structural classifications can yet be recovered automatically.

Code review identified a separate robustness follow-up: the existing native
validator walks each surface's index span independently, so highly overlapping
spans can cause excessive repeated work before the new analysis budget begins.
The report guide states this boundary. The older workbench hardware-discovery
query also still needs bounded output and superseded-query handling. These remain
open rather than being counted as fixed by this task.

As newly requested, fast-forwarded `main` to include all completed development and
pushed the previous task commits to `origin/main`. The working agreement now keeps
development on `main`, with a commit and push after each validated round. The
packaged 0.3.0 archive remains unchanged; this task updates development source and
locally tested binaries. The broad continuing-development goal remains active.

## 2026-09-30 — Bound shared-index validation work

Closed the repeated index-span scanning gap identified during evidence-report
development. A reusable `IndexRangeValidator` preserves the per-surface relative
vertex limit and first failing reference, while switching repeated long scans to
a compact block-maximum tree. Small/disjoint spans retain direct checks without
cache allocation. Unused invalid values remain permitted; negative indices and
large values used by a stricter surface still fail, including with `-force`.

The native-loader cases pass both before and after the change, confirming the
same acceptance and diagnostic behavior. Exhaustive boundary slices and seeded
random overlaps independently compare first failures. A stress test with varying
starting offsets checks 20,000 overlapping ranges with 1,414,304 source-value visits and
30,008 bytes of scratch instead of expanding billions of references. The bound
is checked directly, not inferred solely from a fast machine's elapsed time.

Whole-command comparison on a generated 20,000-surface/120,000-index input gives
0.8392 versus 0.0167 seconds median (50.3× for this range-sharing stress case),
including startup, validation and statistics printing. Five measured alternating
runs follow warmup. Four private native maps retain identical statistics and
unchanged source bytes; their small, inconsistent timing differences do not
support a general native-loading speedup. The [performance guide](PERFORMANCE.md)
links the complete observations and identities. Private inputs/logs remain in
`build/index-validation-*`; build/test logs are in `.agents/tmp/continuation`.

Windows and Linux release each pass thirteen relevant regression groups
(9.61/47.85 seconds); Linux ASan/UBSan passes eight (189.62 seconds, with the
existing documented CLI leak setting). The final helper also checks size-overflow
arguments. [Validation evidence](validation/index-validation.json) records the
34 strict/forced native-loader cases on all builds, the independent work/storage
contract, executable/source identities and before-change compatibility checks.

Separate review found an unchecked copy into a fixed 1,024-byte path buffer in
the older `-info` command. That path-handling issue remains a follow-up; it is
outside the index-range change and is not hidden by this task's successful tests.
Workbench hardware-query bounds/supersession also remain open. No external code
or dependency was introduced. The 0.3.0 packaged archive stays unchanged; the
continuing goal and inference/optimization roadmap remain active.

## 2026-09-30 — Correct the CLI path-overflow finding

Attempted to reproduce the prior `-info` finding under ASan. The existing `Args`
constructor rejects every command argument longer than 1,000 bytes before stage
dispatch. `BSPInfo` receives at most that length and appends/replaces a four-byte
extension in its 1,024-byte buffer, so the claimed CLI overflow is not reachable
through this path. The earlier static review omitted this caller-side guard;
its recorded open finding is superseded by this correction, not counted as a
newly repaired vulnerability. No compiler code change is needed here.

Added explicit `-info` cases at 999/1,000/1,001/1,020/2,048 bytes to the existing
CLI regression group. Accepted argument lengths reach a controlled missing-file
error; larger arguments fail at the global guard. All must exit normally with
status 1 and without sanitizer failures. Evidence is recorded in
[the path-bound verification](validation/cli-path-bounds.json). The confirmed
hardware-query output/supersession task remains the next GUI change.

## 2026-09-30 — Bounded hardware discovery and device details

Replaced the workbench's unbounded, merged-channel hardware query with an
asynchronous `DeviceInventory` client. It limits combined stdout/stderr to
1 MiB, retains at most 8 KiB of compiler messages, validates schema and field
types, requires a normal successful exit, and enforces a fifteen-second deadline.
Cancellation closes output channels and terminates the child; generation checks
prevent stale results and startup errors from replacing a newer query. Changing
the compiler cancels discovery and clears its rows/report. Refreshing also works
after a compiler is replaced at the same path.

The hardware page now lists device indices, names, vendors, memory and compute
units. Selecting a row shows OpenCL capabilities and shared/separate host memory;
the original validated fields remain in a JSON tab. Errors and bounded driver
messages appear in the details pane. Empty inventories explain the GPU's absence
while keeping CPU workflows available. Driver strings are displayed as text;
memory figures and compute-unit counts are not used to rank unlike devices.

Windows and Linux release builds pass the six relevant workbench regression
groups. The new client tests cover eleven query modes, 27 schema rejections,
exact output/diagnostic boundaries, mixed-stream overflow, crashes, startup
failure, cancellation, supersession, terminated abandoned children and recovery
after errors. The real Windows driver reports four OpenCL entries (including
translation-layer entries); Linux exercises the no-platform result. The window
test also uses two synthetic devices for consistent selection and lifecycle
coverage on either host. Direct Qt painting verifies standard/compact layouts,
plain-text device names, all table columns and light/dark themes, without OS
capture or input injection. The test fixture disables Windows CRT newline
conversion so byte-boundary cases measure actual transport bytes.

[Validation evidence](validation/hardware-inventory.json) records build/source
identities, outcomes and preview hashes. Local logs remain in
`.agents/tmp/continuation/hardware-*.log`; generated fixtures/previews stay in
each build's designated test directories. No external code or dependency was
introduced. The 0.3.0 packaged archive remains unchanged.

Separate review confirmed that game-catalog discovery discards stderr without
counting it against a combined output budget. Its retained stdout has a size
guard and it has a deadline, but combined stream accounting remains a follow-up
in the plan. Decompiler inference and intelligent VIS/geometry optimization also
remain active roadmap work; this GUI task does not implement those algorithms.

## 2026-09-30 — Account for both game-catalog output streams

Closed the hardware round's separate discovery follow-up. The game-catalog
client now counts stdout and stderr against one 1 MiB budget, reads only the
remaining allowance plus an overflow-detection byte, and stops retaining output
after failure. Stderr remains excluded from JSON parsing. A superseded child
is also stopped if it completes startup later; timeout/read-error paths use the
same channel shutdown, without replacing an earlier overflow diagnostic.

Eight synthetic query modes cover ordinary/malformed replies, large stdout,
valid JSON followed by excessive stderr, mixed-stream overflow, the exact
combined limit, one byte over it and a stalled process. Real catalog/profile
checks, aliases, supersession, terminated abandoned children and recovery after
failure also pass. Four affected workbench groups pass on Windows (4.26 seconds)
and Linux; [the validation record](validation/catalog-output-budget.json) includes
the Linux time, source/binary hashes and local log paths. No compiler stage or
packaged archive changed. No additional unrelated issue was found in this fix;
the remaining recovery/inference and optimization roadmap remains active.

## 2026-09-30 — Preserve detail flags during fast MAP recovery

The known-source inference audit exposed a concrete recovery defect: every fast
MAP writer emitted zero detail bits, although the full converter already had a
leaf-reference classification policy. Rebuilding fast output could therefore
turn intended detail into structural geometry. All three fast writers now use
the full converter's policy, including its brush-shader structural override.
The recovery report adds optional `detail_classification` metadata with exported
flag counts, policy name/scope and an explicit statement that original author
classification is unproven. Full MAP output on the reproduced input is byte-identical
to the previous executable.

A separate 25-pillar reproduction demonstrates the impact: the old fast output
rebuilds from 19 nodes/4 clusters to 155 nodes/84 clusters. The repaired output
retains 19/4, all 25 world detail flags, exact brush geometry/materials/contents
and the sampled partition/PVS relationships at 768 world points. This is a
specific fidelity repair, not an intelligent portal-optimization speedup claim.

The reusable corpus now covers structural/detail pillars, a mixed authoring
group and translucent structural-override brushes, with 24 recoveries across
three formats and fast/full mode. All labelled world brush flags, exact per-model
brush geometry/materials/contents, sealing and surviving door links are checked.
Spatial comparisons cover 594 points and 352,836 ordered pairs per rebuild,
without assuming persistent cluster IDs. Fast/full partition/PVS parity is
required; source/rebuild differences are recorded separately. Four missing-VIS
cases and 1/4-worker output parity also pass. Seven release regression groups
pass on both Windows and Linux; six pass under Linux ASan/UBSan, including the
full new corpus and native recovery/output-failure checks. Platform results and identities
are in [the validation record](validation/fast-detail-recovery.json); project-local
logs and before/after reproductions remain under `.agents/tmp/continuation`, and
the corpus stays in each build's `tests/recovery-classification` directory.

The audit found two remaining fidelity limits. Ordinary recovery can change
partitioning/PVS despite exact brush geometry (the opaque structural control
changes 71 nodes to 73 and adds/removes 32/8 sampled visibility pairs). The current
leaf heuristic can also mark brush-entity geometry as detail although the authored
MAP did not; that flag does not establish source metadata. Both remain part of
the inference/rebuild-comparison work. Original groups still cannot be extracted
from the compiled fixtures. Existing `UnsortedSet` compiler warnings remain
unchanged. No external code/dependency was added; the 0.3.0 package stays unchanged.

## 2026-09-30 — Compensate for brush insertion order during recovery

Added explicit `-brush-order bsp|rebuild` to all MAP recovery formats and the legacy
conversion interface. The default BSP-record order stays unchanged. Rebuild order
reverses opaque brushes and preserves translucent sequence within each model,
compensating for the MAP loader's front/back insertion. Classification uses the
exported sides' current shader definitions, excluding redundant sides with no
winding. Native recovery-only profiles and `-wtf` material replacement are rejected
for this option. Optional report metadata names the policy and its assumptions
without claiming original editor order or guaranteed rebuild equivalence.

The earlier ordinary partition/PVS differences were caused by tied splitter
choices after brush-order reversal. The new corpus checks 38 rebuilds across
fast/full and three MAP formats, including mixed opacity, brush-entity origins,
redundant translucent sides, missing/invalid options and unsupported profiles.
Five positive cases preserve compiled brush order, exact brush geometry/materials/
contents, surviving entities, node counts and sampled partition/PVS relationships
at 594 points. Thirty-six default MAP exports also match the previous executable
byte for byte; legacy and worker-count parity checks pass.

A deliberately contradictory structural/detail fixture still changes 69 nodes to
74, with 4,494 sampled partition-pair differences and unchanged sampled PVS. An
independent source control proves this residual comes from discarded numeric detail
bits on structural sides affecting splitter priorities, rather than insertion
order. The option does not guess these lost flags. Brush-model detail ambiguity
and broader inference/review work remain open.

Validation: eight release regression groups pass on Windows (23.88 seconds) and
Linux (81.48 seconds); seven pass under Linux ASan/UBSan (359.77 seconds). Measured
fidelity results agree across platforms; source BSP byte identity is not assumed.
[The validation record](validation/recovery-brush-order.json) retains source/binary
hashes, corpus results, baseline output comparisons and the investigative controls.
Useful logs and baseline/probe artifacts remain in `.agents/tmp/continuation`, with
the reusable corpus under each build's `tests/recovery-order` directory. No new
unrelated issue was found; existing `UnsortedSet` warnings remain. No external code
or dependency was added. Workbench exposure follows as a separate round, and the
0.3.0 archive remains unchanged.

## 2026-09-30 — Workbench recovery ordering and capability discovery

Added a dedicated Recovery tab with MAP format and saved BSP/rebuild brush-order
choices, explanatory guidance and command-preview integration. Project schema 1
stores the optional policy and defaults older files to ordinary BSP order, keeping
their previous command unchanged. Run snapshots retain the choice, and only
decompile jobs receive the argument. A real queued recovery verifies the compiler's
reported policy as well as the generated MAP.

The compiler's schema 1 game catalog now advertises optional
`recovery_brush_orders` per profile. The client validates the bounded identifier
list and treats older catalogs as BSP-order-only, so native writing capability
alone cannot enable an option absent from an older executable. Current writable
profiles advertise both policies; the six native recovery-only profiles advertise
BSP order. The UI preserves an incompatible saved selection, explains the issue,
disables unsupported selection/run controls and rejects menu/queue actions before
staging. Switching back to a compatible profile restores availability.

Six affected regression groups pass on Windows (6.95 seconds) and Linux (15.15
seconds), covering real jobs, project migration, malformed metadata, catalogs,
inspection and hardware. The actual offscreen window exercises native and unknown
profiles, retained settings and blocked menu execution. Direct QWidget renders in
both themes at 1380×920 and 1024×720 exposed a clipped compact note; adjusted
spacing and shorter wording fix it. Two affected groups pass again after that
polish on Windows (3.28 seconds) and Linux (5.81 seconds), with explicit checks
that compact controls/guidance need no scrolling. No mouse/keyboard injection or
OS capture was used.

Final catalog checks also anchor identifiers to the complete string, rejecting
trailing newlines. The catalog group passes again on Windows (0.61 seconds) and
Linux (1.68 seconds); the validation record retains each phase's log and identities.

[Validation identities and renders](validation/workbench-recovery-controls.json)
reference the project-local logs and each build's `tests/workbench/window-state`
images. No new unrelated issue was found; the existing `UnsortedSet` warning
remains. No external code/dependency was added, and the 0.3.0 packaged archive is
unchanged. The broader inference and intelligent compiler roadmap remains open.

## 2026-09-30 — Convex brush-cell evidence and optional detail inference

Added `-bsp-evidence -brush-cells` to clip bounded world-brush interiors through
the actual BSP tree, independently of stored leaf-brush references. The analysis
records open/opaque interior witnesses, clearance, fragment/volume consistency
and stored PVS relationships. Double-precision clipping has explicit coordinate,
geometry and work limits; up to 32 tasks distribute uneven brushes through the
persistent job pool. Thin, unavailable or ambiguous geometry produces explicit
fallback status rather than an unsupported classification claim.

Added `-detail-policy legacy|cells` and `-detail-max-work` for MAP recovery. Legacy
remains the default. The cell policy considers current exported-side materials,
protects structural/hint/sky/liquid/portal and other special semantics, preserves
brush-entity and ambiguous baseline flags, and records every input-brush decision
in the optional schema 1 report extension. Inconsistent PVS prevents geometric
promotion. The policy supports three formats, fast/full recovery and legacy
conversion with an automatic report. Native recovery-only profiles support
read-only cell evidence; inference export requires a writable profile. Current
shader assumptions and unproven authoring/rebuild equivalence are explicit.

The new corpus evaluates 90 labelled world brushes and independently verifies
195 interior witnesses. It repairs 19 legacy classification errors across
deliberately removed detail references and structural semantics present only on
a non-first side. Thirty-six rebuilds preserve exact compiled brush geometry,
order, materials, contents and surviving entities, with matching node counts and
spatial partition/PVS relationships at 594 points (352,836 ordered pairs). Oblique
half-cubes/tetrahedra, overlaps, thin slivers, translated solids, bounds, shared
trees, single leaves, six native readers and relocated early roots add independent
controls. Work budgets and the actual 64 MiB report ceiling preserve previous
outputs. One/four-worker reports and MAPs agree; 36 default MAP exports also match
the preserved `39a8ec5` executable byte for byte.

Nine existing regression groups pass on Windows and Linux release builds. The
final new-feature group passes in 10.75 and 36.76 seconds respectively; the first
development runs corrected test assumptions about malformed planes, contradictory
axial bounds and `.map` basename resolution. Nine Linux ASan/UBSan groups pass in
581.92 seconds with the established legacy leak-detection exclusion. Fidelity
metrics and analytical values agree across all three builds. A separate 448-brush
probe confirms parallel report parity; its small local timings, collected during
other validation, do not establish a general decompilation speedup.

[Validation identities and results](validation/brush-cell-inference.json) retain
the exact source/binary hashes, executed checks and compatibility/profiling
evidence. Useful logs/probes remain under `.agents/tmp/continuation/brush-cells-*`
and generated fixtures under each build's `tests/brush-cells` directory. No new
unrelated issue was found; existing `UnsortedSet` warnings remain. No external
code/dependency was added, and the portable 0.3.0 archive is unchanged. Complete
leaf adjacency/PRT use, broader calibration, group/light inference, saved user
overrides and workbench review remain active roadmap work.

Cleanup limitation: automatic approval review rejected removal of the disposable
initial `.agents/tmp/continuation/brush-cells-probe` directory, reporting only
"blocked by policy". That directory remains alongside the retained final evidence.

## 2026-09-30 — Surface-supported group recovery and fast plane precision

Added optional `-group-policy none|surfaces` and `-group-max-work`. Closely coplanar
brush-face overlap with shared rendered surfaces supports deterministic world
assembly proposals, independently of detail flags. Accepted proposals export as
named `func_group` entities with recovered worldspawn compile parameters and a
mandatory report. Brush entities and patches keep their owners. Sensitive
materials, ambiguous ownership/support, outer boundaries and context-dependent
world settings protect against unsupported regrouping. The ordering planner
accounts for both opaque brush reversals during loading/group collapse, preserves
translucent order and rejects proposals that would change the recoverable sequence.

The report records brush/surface links, bounds, exclusions, group status, export
order, work and copied keys. Original grouping, names and lost parameters remain
unproven. An ordinary flat world can produce the same supporting surfaces; a
deliberately flat-source control demonstrates this ambiguity. A disconnected
assembly without shared support remains flat, and its unassociated brushes can
prevent another proposal from exporting without changing order. The option does
not create filler groups to conceal this limit. Current shader assets remain an
assumption, with bounded geometry, work and report size. The workbench does not
yet expose the new policy.

The new corpus checks 42 fast/full rebuilds across three formats, covering mixed
detail/structure, mixed and fully translucent groups, sloped geometry and baseline
compile parameters. Exact ordered brush geometry/materials/contents, surviving
entities, node counts and spatial partition/PVS relationships agree at 650 points
(422,500 ordered pairs per rebuild). Three full Valve 220 controls retain exact
baked lightmaps/lightgrids. Detail-policy independence, one/four-worker parity,
legacy conversion, invalid options, work/hard limits and prior-output preservation
are checked separately. An independent planner test checks 6,000 generated layouts,
718 accepted groups and 34 conflicting-order decisions, including exact work limits.

The sloped control exposed a preexisting fast-export precision defect: a float
tangent basis printed to three decimals changed stored brush planes. Nonaxial
points now solve the dominant coordinate in double precision and print 17
significant digits, preserving axial formatting. Five additional rotated controls
cover the remaining dominant-axis/sign orientations without grouping enabled.
Against the preserved `018e086` executable, 39 default MAPs remain byte-identical;
three fast sloped outputs intentionally change and now rebuild exact ordered
geometry in all formats, where the prior outputs did not.

Twelve relevant release regression groups pass on Windows (65.32 seconds) and
Linux (136.27 seconds). Their grouping fidelity matrices agree, as do the three
controlled bake byte results. Eleven Linux ASan/UBSan groups pass in 778.48 seconds
with the established legacy leak-detection exclusion; its grouping fidelity
matrix also agrees. These durations include concurrent validation and are not
performance benchmarks.

[Validation identities and results](validation/recovery-groups.json) retain the
source/binary hashes, generated input identities, platform checks and preceding
executable comparison. Logs, comparison scripts and investigative probes remain
under `.agents/tmp/continuation/recovery-groups-*`, with reusable fixtures in each
build's `tests/recovery-groups` directory. No new unrelated issue was found;
existing `UnsortedSet` warnings remain. No external code/dependency was added, and
the 0.3.0 packaged archive is unchanged. Broader grouping/parameter inference,
light reconstruction, saved overrides, workbench review and the independent
intelligent VIS/geometry optimizers remain active roadmap work.

## 2026-09-30 — Saved workbench inference controls and capability checks

The Recovery tab now saves independent detail and group policies plus separate
analysis budgets. Legacy detail and flat world geometry remain the defaults.
Selecting surface-supported groups chooses rebuild order and prevents a conflicting
BSP-order selection until grouping is turned off. Expanded work-limit controls
retain their values when inactive, while only active inference options reach
decompile jobs. Project schema 1 validates the optional fields and group/order
relationship; older projects keep their prior command. Run snapshots retain the
selected policies and exact work limits.

The compiler's schema 1 catalog now advertises optional detail and group policy
arrays alongside brush ordering. All 19 writable profiles advertise the new
policies; six recovery-only readers advertise the baselines. The client validates
bounded identifier arrays and treats missing metadata as no inference support.
Grouping also requires advertised rebuild order. A shared capability check drives
both control state and execution guards. Unsupported saved selections remain
visible, guidance gives a usable next action, and menu/run/queue paths reject the
combination before creating a run directory. Switching back to a compatible
compiler restores availability.

The actual Qt queue now builds a fixture containing two labelled assemblies,
recovers both using the saved inference settings, and checks the real compiler's
report and staged project snapshot. Tests cover migration, malformed policy and
budget fields, numeric boundaries, conflicting settings, 25 catalog mutations,
and absence of inference arguments from other workflows. Offscreen window actions
verify persistence, previewed budgets, automatic ordering, native and older
compiler rejection, retained selections, and no output-directory creation on
rejection. Ordinary recovery remains available with older catalogs.

Visual inspection at 1380×920 and 1024×720 in both themes found that the initial
stacked controls did not fit and expanded limits could squeeze dropdown text
away. The final layout pairs the choices in two columns and propagates its content
minimum to the scroll area, keeping dropdowns readable. Main choices fit the
compact view; expanded limits scroll there and fit together at full size. Tests
check control height and access to the complete last field, settle Qt layout
events, and render the application's own widget tree. No OS capture or input
injection is used. The existing 5 MiB report-preview limit now has a visible note
pointing to the complete files in the run folder.

Six affected test groups pass on Windows and Linux across the recorded phases.
The final three GUI/queue checks pass in 4.34 and 7.83 seconds respectively;
catalog checks and unchanged inspection/device clients have separate successful
records. [Validation identities, reports and renders](validation/workbench-inference-controls.json)
retain the exact source/binary hashes and distinguish final checks from superseded
layout/test-harness failures. Logs and the recording helper remain in
`.agents/tmp/continuation`; generated projects, queued outputs and direct renders
remain under each build's `tests/workbench` directory.

No new unrelated issue was found and no external code/dependency was added. The
0.3.0 archive is unchanged. These controls expose inference export; proposal
overlays, saved per-brush corrections, broader reconstruction and the independent
intelligent compiler workstreams remain active roadmap work.

## 2026-09-30 — VIS merge repairs and compact working bitsets

The M9 baseline audit found that reverse PRT directions lost hint/sky flags,
leaf unions could overrun their 1,024-entry arrays, and portal joins used the
opposite normal for convexity. Both directions now retain the file flags, hints
cannot be bypassed through a second opening, and capacity is checked before
either leaf is changed. Polygon joins require compatible planes/flags and the
correct winding normal; a union exceeding 512 points remains separate, while a
valid 512-point result can use larger combined input counts. Self-edges fail
before traversal, and already merged leaves are not revisited by `-hint`.
`-hint` and `-merge` now have the same documented behavior.

Live portal directions receive dense working-bit indices after the selected
merges. Front/flood/flow/passage vectors and their scans omit removed bits, while
portal objects, sort order and original job slots remain unchanged. This retains
the fixed publication batches used for deterministic pruning. Cluster mapping
and runtime VIS layout are preserved. The fast path releases its unused flow
buffer; statistics count self visibility once, and passage diagnostics count all
live directions with wide arithmetic. PRT removal now follows successful BSP
publication, preserving the retry input on an output error.

Thirty-one graph controls cover both hint orientations, multiple openings, sky
distance-cull exceptions, convex/concave/noncoplanar joins, point/leaf bounds,
complete contraction and every solver. Two self-edge failures preserve the BSP.
Windows sharing locks and Linux file-size limits verify failed-publication
preservation and successful retry. Matched poor-detail/manual-detail MAPs add
120 checks across four solvers, default/merge/mergeportals/hint plus unsorted
merging, and 1/4/20 workers. Non-entity/non-VIS lumps remain unchanged. The complete
graph and visibility matrices agree on Windows, Linux and Linux ASan/UBSan.

Every final Windows graph/map case also matches a preserved repaired-but-unpacked
compiler byte-for-byte. An earlier 96-run matrix includes 32 runs at 70 workers,
also with reference parity. A committed source patch reconstructs the unpacked
reference from `7aaa22b`; applying it was independently checked against the saved
source. Five probes reproduce preceding-executable defects in sky visibility,
hint protection, folded and concave joins, and a valid winding union rejected by
the old allocation count. These repairs are distinct from bit compaction's
unchanged-output contract.

The alternating grid=9 benchmark covers 48 configurations, one warmup/five
measured runs, and the preceding executable. In structural `-merge`, 1,348 of
1,436 directions survive: working bitsets shrink 184→176 bytes, and requested
passage storage falls 3,469,600→3,330,816 bytes (4.0%). One-worker whole-command
medians are 2.0696→2.0533 seconds, and 20-worker medians 0.3406→0.3392 seconds.
There is no broad speedup claim. All measured compact/uncompressed VIS bytes
agree, while legacy merge/hint still omit 27 default PVS pairs on grid=9 (two
on grid=5). This does not satisfy the intelligent optimizer's baseline-inclusion
gate, so automatic regional merging remains planned rather than enabled.

Eight relevant release groups pass on each platform across the recorded phases;
six ASan/UBSan groups pass in 304.96 seconds with the established process-lifetime
leak exclusion. The expanded sanitizer VIS matrix takes 169.97 seconds; its CTest
timeout is now 360 seconds to leave headroom. The final added graph control and
unsorted/reference matrices have separate successful records. See the
[validation identities/results](validation/vis-portals.json), [raw benchmark](benchmarks/vis-portals-win-x64.json)
and [VIS guide](VIS.md). Useful logs, reference binaries/source and probes remain
under `.agents/tmp/continuation/vis-portals-*`; reusable generated fixtures remain
in each build's `tests/vis-merge` directory. The disposable patch-check tree and
test-created partial staging files were verified and removed.

Two additional findings remain open. Windows commands sometimes spend 5–21
seconds outside their named VIS passes at 20/70 workers, including preceding
executables; a 90-second stress timeout was followed by successful isolated and
full-matrix retries. Raw slow benchmark samples are retained. Separately, Linux
fatal BSP writes can leave a partial staging file because `SafeWrite` exits before
the staging destructor. The first test's no-leftovers assertion exposed this;
final tests record and remove only their own partial file while requiring the
BSP/PRT preservation provided here. Shared writer cleanup and localization of the
Windows delays are explicit follow-ups in the plan. Existing `UnsortedSet` build
warnings remain. No external code/dependency was added, no game/input/capture
automation was used, and the 0.3.0 archive is unchanged.

## 2026-09-30 — Checked BSP and buffer-write cleanup

Repaired the failed-write staging leak exposed by the preceding VIS round. Both
native BSP serializers now borrow streams owned by `OutputFiles`; shared
`SaveFile` buffers use the same owner. Checked writes, 64-bit seeks/positions,
lump-range errors, buffered close failures and replacement failures unwind before
the fatal compiler diagnostic. Streams close before staging files are removed,
and existing destinations survive. Removed the unused fatal `SafeWrite` and
`SafeClose` helpers. Shader remapping finishes before opening output, and byte
order restoration no longer repeats it. BSP success messages follow publication.

Added empty-world, ordinary and large-lightmap IBSP/RBSP failure/retry fixtures,
directory/link guards and independent profile-save failures. Linux exercises
actual short writes, buffered header-seek and close failures with file-size
limits; Windows denies replacement with sharing locks. The output-owner unit
test covers binary overwrite, a real read-only stream error, argument rejection
and 64-bit positions while retaining a five-byte file. VIS now requires no new
staging files after failure; its test-side cleanup workaround is gone.

Final validation: 11 release CTest groups pass on Windows (24.10 seconds) and
Linux (70.02 seconds); seven ASan/UBSan groups pass (216.32 seconds), with the
established process-lifetime leak exclusion. There are 10 Windows and 13 Linux
failure/destination checks, with the 13 repeated under sanitizers. Windows cannot
create the three test symbolic links without additional privileges; Linux covers
all three. Six successful rewrites on each release platform match the preceding
`5af3d3f` executable byte-for-byte after masking only the unused timestamp.
The final 31-graph/120-map VIS checks pass on all three builds, with identical
real-map PVS matrices and preserved BSP/PRT retry inputs. No unfinished stage
remains in the checked test areas.

See [output guarantees](OUTPUT-SAFETY.md) and [validation identities/results](validation/checked-writes.json).
Useful build/test logs, before/after evidence and preceding executables remain
under `.agents/tmp/continuation/checked-writes-*`; generated fixtures remain in
the designated `build/*/tests` directories. No performance gain is claimed.

Separate findings remain open: intermittent Windows process delays outside VIS
passes, remaining raw sidecar writers, and inherited ZIP uninitialized-value,
`UnsortedSet` layout, deprecated `u8path` and lightmap path-format build warnings.
This round does not provide crash recovery or a transaction spanning every
compiler output. No external code/dependency was added, no game/input/capture
automation was used, and the packaged 0.3.0 archive remains unchanged.

## 2026-09-30 — Regional portal and stored-visibility diagnostics

Added optional `-bsp-evidence -portals matching.prt` analysis, linking an explicitly
supplied PRT graph with the BSP's existing regional frontier. Reports rank regions
by candidate passage-pair work, distinguish internal/boundary openings, expose
hint/sky/unknown flags and graph bridges, measure small/slender windings, and
retain bounded local world-brush associations. Stored PVS rows union world surface
IDs before counting indexed triangles and patch surfaces. Repeated leaf and
surface references do not inflate these costs; padding bits are ignored.

The CLI and normal VIS now share a bounded PRT1 reader with checked counts,
indices, self-edges, finite coordinates, parentheses, total points/bytes and
complete records. Directed winding order and existing VIS solvers remain intact.
The parser's 7,970 seeded decimal/scientific comparisons match legacy `scanf`
binary32 results; 2,094 truncated, malformed and over-budget inputs are rejected.
PRT/BSP identities are checked before/after analysis, input aliases are protected,
and work exhaustion preserves the previous report without leaving staging files.

Independent graph edge-removal, regional traversal, triangle-fan shape and PVS
set-union oracles cover Quake 3 and Raven controls. Additional cases exercise
shared nodes, 512 paths to one leaf, parallel openings, flags, stale geometry,
unusable windings, missing/mismatched VIS, exact/exhausted work limits and combined
brush-cell analysis. Reports agree exactly at one/four workers. The nine-column
structural controls have 40 clusters, 94 openings and 816 candidate ordered pairs;
manual detail reduces these to 4, 4 and 8. An 81-column Windows control reports
220, 718 and 13,586 versus the same detail counts. These are diagnostic contrasts,
not claimed speedups or automatically accepted transformations.

Six relevant release CTest groups pass on Windows (41.71 seconds) and Linux
(53.46 seconds); six ASan/UBSan groups pass (308.98 seconds), including the full
VIS matrix, with the established process-lifetime leak exclusion.
The 31-graph/120-map VIS reference
matrices match the preceding `2295427` executable on both release platforms, and
their graph/PVS semantics agree across platforms. The Windows reference phase
precedes only expansion of the evidence warning message; the final Windows
release groups and larger control use the final executable. Detailed final
sanitizer results and identities are recorded in
[the validation record](validation/portal-evidence.json).

See [regional portal diagnostics](PORTAL-ANALYSIS.md) for usage, field definitions,
thresholds and limits. Center probes cannot prove BSP/PRT correspondence; brush
samples do not identify the source cause of a cut. Reports expose unavailable
mapping and disagreements, and warn about misleading inputs. Automatic regional
rectification, validated source changes and workbench overlays remain open in
M9. Useful logs and preceding binaries remain under
`.agents/tmp/continuation/portal-evidence-*`; generated fixtures remain under
`build/*/tests/portal-evidence*` and `build/*/tests/vis-merge`.

Separate findings remain open: intermittent Windows delays outside VIS passes,
remaining raw sidecar writers and inherited build warnings. This build also
reports a GCC bounds warning when inlining the pre-existing copied argument
vector in `BSPEvidenceMain`; the relevant argument and sanitizer controls pass.
No external code/dependency was added, no game/input/capture automation was used,
and the packaged 0.3.0 archive remains unchanged.

## 2026-09-30 — Compact passage masks and bounded candidate work

Passage construction now intersects preliminary flood masks word by word, visits
only surviving candidate bits in their original order and skips separators for
empty intersections. Replaced per-passage linked allocations with eight-byte
descriptors and trimmed word spans in one block per source portal. Flow clears
omitted words in reused scratch, retains the previous clipping/solver order and
frees passage blocks after the joined flow stage. Construction initially reserves
the bounded dense size per active job; an unsuccessful shrink retains the valid
original block. Logs distinguish retained requested bytes, dense equivalents,
empty masks, retained blocks and candidate visits from peak process memory.

On the structural grid=9 fixture, default passage storage falls
3,004,400→1,284,000 bytes (57.3%); existing merged storage falls
3,330,816→1,311,152 bytes (60.6%). The default candidate scan visits 2,386,961
portals instead of 21,571,592 dense entries. Five alternating measurements after
warmup cover full/passage-only, normal/merged, structural/manual-detail and one/four
workers. Single-worker passage-only medians improve 0.2932→0.2427 seconds without
merging and 0.2437→0.2053 with merging. Full-flow changes are small; four-worker
passage-only complete commands are slower in this run despite faster construction.
No general speedup or peak-memory claim is made. All 192 benchmark VIS commands
retain reference/worker byte parity and unchanged non-entity/non-VIS lumps.

The 8,400 mask controls compare packed round trips/intersections with dense
results, poisoned scratch and boundary guards up to 2,048 words. Thirty-one graph
and 120 real-map cases retain exact preceding-executable VIS bytes on Windows
and Linux, with matching graph/storage/PVS results under ASan/UBSan. The extended
matrix checks mask/block/candidate accounting and the two non-passage solvers.
Eight additional capacity controls cover a 1,024-degree star with 1,049,600 empty
descriptors and a 130-opening chain. Both solvers and one/four workers agree with
independent expected PVS rows on all three builds. The empty-star control retains
8,396,800 bytes against a 285,491,200-byte dense equivalent; it is a synthetic
storage control, not a spatially matched map or runtime-performance claim.

Five initial release CTest groups pass on Windows (38.97 seconds) and Linux
(58.02 seconds), followed by final unit/capacity checks (1.25 and 1.59 seconds).
Five ASan/UBSan groups, including the full VIS matrix, pass in 244.56 seconds;
final unit/capacity checks pass in 13.02 seconds. The established process-lifetime
leak exclusion remains; address and undefined-behavior checks are enabled.
Only whitespace cleanup and test/documentation changes followed timing runs.
See [performance measurements](PERFORMANCE.md#passage-construction-and-retained-storage),
[raw benchmark](benchmarks/vis-passages-win-x64.json) and
[validation identities/results](validation/vis-passages.json).

The initial automatic-merge investigation confirmed that leaf merging alone can
omit baseline bits: on grid=9 it omits 20 full-flow bits even with polygon merging
disabled (the existing combined merge omits 27). This comparison does not prove
which set is geometrically exact; it prevents treating polygon-merge exclusion
as a sufficient baseline-inclusion gate. The temporary probe switch was removed.
Automatic regional changes remain open, with the full requested scope retained.

A separate code finding needs follow-up: passage clipping still truncates windings
above its inherited 24-point scratch capacity. This round preserves that geometry
path for representation parity rather than claiming to repair it. Existing raw
sidecar writers, intermittent Windows process delays and inherited build warnings
also remain open; these rebuilds emit `UnsortedSet` layout warnings. Useful logs,
probe results and dense reference binaries remain under
`.agents/tmp/continuation/{passage-spans-*,smart-vis-*}`; generated tests remain in
the designated build directories. No external code/dependency was added, no
game/input/capture automation was used, and the 0.3.0 archive is unchanged.

Automatic approval review rejected cleanup of `tests/__pycache__`, reporting
only “blocked by policy.” Read-only inspection found 24 generated `.pyc` files;
the cache was left untouched and the deletion was not retried. This restriction
does not prevent committing/pushing the verified source and documentation.

## 2026-09-30 — Preserve complete portal windings during passage clipping

Reproduced an inherited false-culling case with a three-opening chain: the old
default/passage-only solver omitted a directly visible cluster when the final
64-point polygon started at one vertex, then restored it after rotating the same
vertex list. Passage construction copied only the first 24 vertices before
clipping. Replaced that prefix with complete input spans and two alternating,
bounded scratch buffers per worker, without per-candidate allocations or copying
the polygon between cuts. Intermediate convex windings can grow from the
512-point input limit by up to one vertex for each of 1,024 separating planes.
Capacity exhaustion discards partial output, retains the previous complete
winding and emits a diagnostic; it can reduce selectivity but cannot hide geometry
by dropping an arbitrary prefix. Float distance/interpolation order, the original
double epsilon threshold and all-on-plane behavior are preserved.

The large-portal audit also exposed an off-by-one separator-cache check: exactly
512 stored planes caused an error even though the final slot was valid. The
check now precedes writing and allows the full bounded cache. Neither change
enables automatic portal merging or claims a general exact-PVS solution.

The independent half-plane feasibility oracle verifies 1,998 rotated/reversed
convex winding cases (474 visible, 1,524 hidden), excluding one trial inside its
numerical margin. Direct controls cover epsilon, growth beyond 512 intermediate
points, exact output capacity, conservative overflow, a surviving corner outside
a partial prefix, later safe rejection, oversize input and scratch reuse.
The CLI suite contains 288 analytic controls for visible and blocked paths with
24, 25, 32, 64, 129 and 512 points, three solvers, two winding orientations,
cyclic rotations and one/four workers. Six additional native VIS runs use a
sealed 64-sided corridor with four genuine 64-point portals. Native lumps outside
entities/VIS and the PRT remain unchanged.

Against `ac402aa`, 144 Linux reference probes isolate ten old wrong-visibility
results and eight old separator-limit errors. The old binary is not the oracle
for those repaired cases. The ordinary 31-graph/120-map reference matrices retain
exact preceding VIS bytes on Windows and Linux, with matching cross-platform
semantics. The new suite's initial sanitizer run reached the aggregate 180-second
CTest deadline while progressing through hundreds of instrumented subprocesses;
the aggregate limit was raised to 600 seconds while retaining individual child
deadlines. The initial timeout is retained in the validation evidence rather than
treated as a pass.

The final analytic/native controls pass on Windows Release, Linux Release and
ASan/UBSan, with identical graph/PVS semantics across all three. The successful
sanitizer retry finishes in 278.81 seconds; final unit checks pass in 0.27, 0.16
and 7.05 seconds respectively. Existing compiler-pipeline, PRT-validation,
passage-storage and ordinary VIS checks also pass; the established process-
lifetime leak exclusion remains. See [the validation record](validation/vis-clipping.json)
for final executable/source identities, aggregate-timeout history, regression
digests and reference cases. CLI behavior applies equally to workbench-launched
builds; this round does not change the workbench UI or saved project format.

Five alternating Windows measurements after warmup cover 16 ordinary-map
configurations and 192 VIS commands. Every measured output and its passage
accounting match the preceding executable and worker counterpart. Default/full
single-worker passage construction falls 0.07115→0.06454 seconds median, with
whole commands 2.3613→2.3189 seconds. Default single-worker passage-only is 2.0%
slower overall; the tiny detail controls have substantial process/scheduling
variation. Full observations and ranges are retained in the
[performance guide](PERFORMANCE.md#large-portal-clipping-repair) and
[raw measurements](benchmarks/vis-clipping-win-x64.json). No general compiler
speedup or peak-memory reduction is claimed for this correctness repair.

Remaining findings include the recursive portal clipper's conservative small-
buffer fallback, automatic-merge baseline-inclusion failures, raw sidecar writers,
intermittent Windows delays outside VIS passes and inherited `UnsortedSet` layout
warnings. The previously policy-blocked cleanup targets were not retried. Useful
probes, preceding executables and logs remain under
`.agents/tmp/continuation/passage-clipping-*`, and fixtures/reports in the existing
build test directories. No external code/dependency was added, no game/input or
capture automation was used, and the packaged 0.3.0 archive remains unchanged.

## 2026-09-30 — Reconstruct bounded world-cell adjacency from BSP geometry

Added opt-in `-bsp-evidence -cell-adjacency` as a shared geometric prerequisite
for detail inference and intelligent regional VIS work. It clips an explicit
world-model enclosure through normalized BSP planes, retains separate leaf-path
cells and intersects opposing face fragments on their originating partition.
Reports include cell bounds/volume, interface polygons/area/endpoints, open
enclosure faces, numerical degeneracies and fixed geometry limits. Analysis does
not depend on stored leaf AABBs, leaf-brush references, PVS or a supplied PRT.
Negative clusters define the opaque category; opaque/opaque interfaces are omitted.
Repeated leaf records remain distinct path cells. Shared internal nodes disable
the analysis rather than expanding ambiguous paths.

The core and traversal are iterative, handle early formats' native world roots
and share the evidence command's work budget and checked 64 MiB report writer.
Capacity/work exhaustion or an invalid enclosure preserves prior output. The
default report is unchanged: 20 preceding/current Linux comparisons across
plain, brush-cell, PRT and combined modes retain exact report bytes. This stage
is serial, makes no performance claim, does not substitute reconstructed geometry
into VIS, and does not change decompiler policies or the workbench UI.

The independent unit oracle checks 24 generated box trees with 64 cells each,
covering 4,772 adjacency/area and rejection checks. Additional controls cover
scaled oblique planes, single/shared leaves, thin slabs, cap-collapse disclosure,
exact geometry capacities and a 10,000-node tree. CLI controls compare every
reported interface with the matched compiled PRT by endpoints, area and symmetric
convex containment, while checking its vertices against both actual native paths.
Quake 3/Raven structural/manual-detail pairs, the 64-sided corridor, all six
recovery-only readers, relocated/single-leaf roots, modified leaf bounds, absent/
present VIS, one/four workers, combined analyses and protected failures pass.

Windows Release, Linux Release and ASan/UBSan all pass the two new groups plus
existing compiler-pipeline, BSP evidence, brush-cell and portal-evidence groups.
The five reconstructed graph objects are exactly equal across the three builds.
The new CLI controls take 5.85, 8.43 and 59.09 seconds respectively; these are test
suite runtimes, not compiler-speed measurements. The sanitizer regression suite
finishes in 401.34 seconds with the established process-lifetime leak exclusion;
address/undefined-behavior checks remain active. A separate Windows grid=9 run
matches 718 open and 726 opaque interfaces for each native writer, with 431 path
cells and one reported degeneracy event. No copyrighted map assets are included.

The oblique control demonstrates why this is not exact original-PRT recovery:
the original float portal construction and clipping of serialized BSP planes
differ by up to 0.0474 plane/edge units and 0.647% face area. Tests retain these
observations under explicit 0.1-unit/1% PRT-comparison tolerances while requiring
native path membership within `2e-6` units. The corridor reports 194 degeneracy
events. Balanced volume does not prove topology, and original hint/sky flags,
author structural causality and completeness outside the enclosure remain unknown.
See [the guide](CELL-ADJACENCY.md) and [validation record](validation/cell-adjacency.json)
for limits, exact identities and executed evidence.

Automatic merge gates remain open: after the preceding clipping repair, combined
merging still omits 2 baseline visibility bits on grid=5 and 27 on grid=9. The
next geometric step is bounded interface/PRT area coverage and protected-flag
correspondence before regional transformation proposals. These diagnostics do not
establish a safe automatic edit. Stripped-light inference, broader authoring
recovery and appearance-preserving triangle optimization remain in the plan.

Other findings include inherited `UnsortedSet` layout warnings and a Linux GCC 13
array-bounds warning while inlining the inherited `Args::getVector` into the
evidence CLI; exercised commands pass, but the warning remains recorded. Earlier
raw sidecar writers, intermittent Windows process delays and conservative recursive
portal clipping remain open. Prior policy-blocked cleanup targets were not retried.
Useful probes, reference executable, scripts and logs stay under
`.agents/tmp/continuation/cell-adjacency-*` and `auto-vis-probe*`; reusable fixtures
and reports stay in their build test directories. No external code/dependency was
added, no game/input/capture automation was used, and the packaged 0.3.0 archive
remains unchanged.

## 2026-09-30 — Exact planar triangle-reduction foundation

Implemented an immutable, bounded reduction core for M10. It removes interior
vertices of simple, exactly planar fans only when all 29 supplied interpolants
are exactly affine. Deterministic candidate ordering and ear clipping preserve
coverage, winding and every boundary subdivision, including collinear T-junction
endpoints. Degenerate/nonmanifold input neighborhoods and self-crossing rings
remain protected; new diagonals are also checked against existing external edges.
Every accepted edit has stable face-history IDs for independent replay. Work,
input size, ring size and history capacities are explicit. Exhausted hard limits
throw without mutating input or publishing partial work.

The new first-party binary32 predicates use homogeneous determinants, exact
two-float products, fused multiplication residuals and error-free sum expansions.
They cover the finite float range without coordinate-subtraction cancellation,
check the required arithmetic environment and reject nonfinite input/fast-math
configurations supported by the compiler guards. This mathematical contract does
not establish shader or finite-precision GPU rasterization equivalence. Neither
the compiler nor workbench invokes the core yet; there is no new reduction switch
or optimized BSP writer in this round.

Twenty flat/sloped grids in both windings reach their boundary-preserving minimum,
with 1,364 interior removals and 4,212 coverage/interpolation checks. Independent
Python rational elimination agrees on 4,136 determinant signs. Thirty-two mesh
oracles cover holes, reversed winding, skew, slope, broad dyadic scales and
one-ULP seams, adding 607 exact interpolation samples. Unit controls cover history
replay, unsupported rounding, malformed data, exact/exhausted limits, concave and
self-crossing fans, duplicate/nonmanifold faces and external-diagonal conflicts.

Original generated OBJ models establish opportunity beyond existing meta
processing: the compiled 512-triangle grid is split into six native surfaces;
read-only core analysis retains their boundaries and finds 146 candidate triangles.
The uniform-ambient bake still permits 146, while the point-light bake retains
496 to preserve the non-affine baked colors. These are mathematical candidate
counts, not enabled optimizations or measured runtime savings. The native IBSP46
controls add 276 rational interpolation samples and verify that analysis leaves
the complete BSP unchanged. Other native adapters remain planned.

Windows Release, Linux Release and ASan/UBSan pass all three new groups plus
compiler-pipeline, portal-graph and cell-graph regressions: six groups per build,
in 4.02, 7.38 and 32.21 seconds respectively. These are test durations. The
rational-oracle reports agree exactly after excluding binary identity. Native
summary multisets also agree, including work/rejection counters and local output
index hashes, after excluding platform-dependent surface IDs. Native compiler
surface ordering and input/BSP bytes differ; no cross-platform compiler-byte
determinism is claimed. Sanitizers use the established process-lifetime leak
exclusion with address and undefined-behavior checks active.

A read-only audit of the reference Quake III renderer found a material eligibility
constraint: projected dynamic-light color is piecewise and quantized at vertices.
Moreover, specialized stage iterators do not perform the generic iterator's
`SURF_NODLIGHT` check, so that flag alone cannot approve retriangulation. This is
an external renderer finding, not repaired here. Fog/deformation/texture-generation,
vertex-color quantization, alpha/blending, remapping and other renderer profiles
also need explicit eligibility checks. The plan now compares pre-LIGHT with
post-LIGHT application because the native point-light control demonstrates the
importance of final baked fields. Native publication, GUI/CLI controls, renderer
validation and the remaining VIS/decompiler/GPU work stay open.

See [the core guide](PLANAR-REDUCTION.md) and [validation record](validation/planar-reduction.json)
for contracts, source/binary identities and exact evidence. Useful build/test logs
and the evidence-recording helper remain under `.agents/tmp/continuation/planar-reduction-*`
and `.agents/tmp/continuation/record-planar-reduction.py`; generated test inputs and
reports stay in `build/<preset>/tests/planar-reduction*`. No external code or
dependency was incorporated, and no game, input or capture automation was used.
No new build warnings arose in this incremental round. Existing VIS baseline-
inclusion failures, raw sidecar writers, intermittent Windows delays, conservative
recursive portal clipping and inherited compiler warnings remain recorded.
Previously policy-blocked cleanup targets were not retried; the packaged 0.3.0
archive remains unchanged.

## 2026-09-30 — Native triangle optimizer and renderer qualification

Added `-optimize-geometry` and two workbench workflows for bounded, post-LIGHT
IBSP46 analysis/publication. The initial contract explicitly targets Quake3e
OpenGL. It admits supported opaque horizontal world surfaces only when both
assets and native flags already disable marks and dynamic lights, normals/colors
are constant, mappings are globally exactly affine and no fog/shared model owner
is present. Other materials and geometry receive explicit protection reasons.
The independent exact reduction core removes interior fan vertices and retains
every boundary segment; separate surface jobs have stable budget allocation and
one/four-worker output parity. Source BSP and complete shader inventory identities
are rechecked before checked BSP/report publication.

The shader scanner inventories unlisted files and duplicate VFS definitions under
file/token/definition budgets. It rejects malformed structure, NUL even inside
comments, overlong renderer tokens and signed-char-dependent non-ASCII tokens;
overlong image paths are protected. VFS reads now accept a pre-allocation byte
limit and reject incomplete packed reads. Runtime remapping and other renderer
contracts remain unsupported. Quake3e's consistent nodlight handling differs from
the original renderer's specialized iterators, so BSP writing requires explicit
`-renderer quake3e-gl`; analysis alone describes that profile without writing a BSP.

Native compiler output exposed shared index subsequences between surfaces. The
new allocator reserves all unchanged owners' slots and places changed indices
only in existing unreferenced contiguous storage. Insufficient storage fails
without publication. Only changed surface first-index/count fields and allocated
index values may differ: vertex bytes, IDs, lump layout, file size, collision,
VIS, ownership, entities and baked data are retained. Tests check the complete
byte boundary, shared-owner/index controls, exclusions, malformed/oversized data,
hard work/storage failures, output rollback, source aliases, loader acceptance and
idempotence. The generated ambient grid falls from 512 to 144 triangles across
six surfaces; the point-light control keeps all 512 because colors vary.

The engine qualification first caught inconsistent winding/normals in the imported
grid fixture; corrected winding makes the reduced face visible. The preceding
core-only 146/496 figures remain historical. The first visible-face matrix also
rejected a broader dynamic-light policy: 19/60 comparisons exceeded the preset
raster tolerance, reaching a two-step channel difference and 2,338 changed pixels.
Restricting candidates to already-authored nodlight surfaces passed the same
tolerance without relaxation. Sixty comparisons cover five cameras/distances,
four lighting cases and three renderer configurations. All twenty repeat controls
are byte-identical; a deliberate 16-unit geometry error changes 71,308 pixels.
The final matrix's maximum is one 8-bit channel step at 61/307,200 pixels, against
a limit of one step and 0.1%. Engine counters confirm 530 → 162 submitted scene
triangles in every view, with 376 vertices retained. This is synthetic count and
finite-raster evidence, not a hardware-GPU, frame-time or universal pixel-identity
claim.

The original cgame fixture calls the reference engine's registered screenshot
command using SDL offscreen, windowed operation, disabled input devices and
software Mesa llvmpipe. No OS capture or input injection is used. Read-only external
assets and GPL-compatible Quake3e headers/build support the optional test; no engine
implementation or game content is redistributed. The [credits](UPSTREAM.md) and
[validation record](validation/geometry-optimize.json) identify the dependency and
source/binary identities. Final captured BSP lumps also match the final parser
build's generated inputs/outputs. Successive Windows fixture builds exposed native
surface/index reordering as well; the matrix was repeated against the exact final
artifacts. This inherited compiler-order issue is separate from fixed-input
optimizer determinism and remains open.

The GUI exposes analysis and optimization through normal staging, queue, logs and
reports, includes the renderer choice in commands and explains the initial scope.
Unavailable catalogs/profiles block run/menu actions before creating output folders.
Actual compiler queue tests and compact offscreen window previews pass on Windows
and Linux. Exclusions and custom budgets currently use the CLI; region overlays,
broader materials/formats and native/hardware rendering validation remain planned.

Broad 62-group Windows/Linux sweeps found one new dispatch regression: putting
`-renderer` before the mode could fall through to ordinary BSP compilation. The
geometry mode now resolves anywhere in remaining arguments, with that ordering
exercised by the preservation test. All other Windows groups passed; Linux also
skipped `area_factors` because a GPU backend was unavailable. After repair, eight
relevant groups passed on both platforms and four native/pipeline groups passed
under ASan/UBSan. The three core groups passed in the preceding sanitizer run.
Subsequent parser-boundary changes were rechecked with both material/native groups
on all three builds. The ledger retains failed runs as failed and records the
successful follow-ups rather than relabelling the original sweeps. Sanitizers keep
the established process-lifetime leak exclusion; address/undefined checks remain active.

See [the user guide](GEOMETRY-OPTIMIZATION.md) and [updated M10 plan](PLAN.md#m10--geometry-optimization-without-presentation-changes).
Inherited `UnsortedSet`/GCC inline warnings, VIS merge baseline omissions, raw
sidecar writers, intermittent Windows delays and conservative recursive clipping
remain open. The stripped-light, broader inference and automatic VIS workstreams
remain active. The portable 0.3.0 archive is unchanged. Useful test artifacts/logs
stay in their build directories and `.agents/tmp/continuation/geometry-*`.

Automatic approval review rejected cleanup of the new disposable renderer probe
and intermediate nodlight-matrix folders/files with “blocked by policy.” They were
verified as project-local with no reparse links but remain in
`.agents/tmp/continuation/geometry-render/`; no alternate deletion was attempted.
Earlier blocked cleanup targets were not retried either.

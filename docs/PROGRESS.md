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

# Implementation plan

## Scope and working agreement

Create a standalone q3mapx compiler from the latest NRC source available at import
time. Retain the CLI, substantially improve performance and robustness, improve
BSP decompilation, and deliver a capable native GUI. Major rewrites are permitted
when measured results and correctness justify them. Commit after each completed
task. A milestone can contain several independently validated task commits.

Development starts on Windows x64; design and build definitions must also support
Linux. Use the available CPU and GPU for validation without changing system
settings or requiring proprietary game content. No game launches or input control
are required for the initial test suite.

## M0 — Plan and provenance

- [x] Identify current NRC upstream and exact revision.
- [x] Inspect licenses before incorporating code.
- [x] Record architecture, implementation order, and acceptance criteria.
- [x] Import the compiler, required libraries, and upstream regression fixtures.
- [x] Add standalone CMake/Ninja presets and a documented build.

Acceptance: a clean configure and release build, working help/game listing, exact
upstream revision recorded, license texts and attribution included. No editor
application or game assets required to build the compiler.

## M1 — Reproducible correctness and performance baseline

- [x] Add original synthetic map fixtures that require no commercial assets.
- [x] Exercise BSP → VIS → LIGHT and BSP → MAP → BSP through the actual CLI.
- [x] Record normalized output invariants, timings, hardware, options, and versions.
- [x] Add a repeatable benchmark harness with warmup and multiple measured runs.
- [x] Add CI for supported build configurations and regression checks.

Acceptance: fresh test outputs stay in designated build/temp directories; failures
include actionable logs. Comparisons use identical fixtures and compiler settings.
Microbenchmarks and end-to-end timings are clearly distinguished.

## M2 — Robustness and BSP decompilation

- [x] Validate BSP lump lengths, ranges, references, strings, and geometry before use.
- [x] Reject malformed numeric arguments and oversized/invalid worker, minimap, lightmap and portal counts before allocation/traversal.
- [x] Repair lost texture-match candidates and ill-conditioned UV reconstruction.
- [x] Preserve brush entities, origins, patches, and recoverable texture transforms.
- [x] Add a convenient decompilation workflow and a machine-readable recovery report.
- [x] Test corrupt files, degenerate geometry, overlapping surfaces, and round trips.

Acceptance: invalid files fail with diagnostics and nonzero status; valid fixtures
round-trip with expected geometry/entities and finite UVs. Unsupported or
irrecoverable data is reported explicitly. See [decompilation design](DECOMPILATION.md).

## M3 — CPU scheduling and profiling

- [x] Replace per-pass thread creation and per-item global locking with persistent workers.
- [x] Use adaptive range dispatch for small jobs while balancing expensive uneven work.
- [x] Remove fixed worker-array limits and validate requested thread counts.
- [x] Keep compiler data locks separate from scheduling/progress locks.
- [x] Add structured per-pass timings and useful build reports.
- [x] Profile expensive compiler stages and optimize measured bottlenecks (minimap brush traversal).

Acceptance: exactly-once execution at varied worker counts, zero/small job handling,
safe shutdown/error propagation, regression parity, and repeatable timing evidence.
Do not equate a scheduler microbenchmark speedup with total compile speedup.

## M4 — GPU compute

- [x] Inventory compute devices and choose a portable optional backend.
- [x] Implement a real batched GPU workload, starting with minimap geometry sampling.
- [x] Retain a tested CPU implementation and explicit backend selection.
- [x] Handle unavailable devices, allocation/build/dispatch failure, and small workloads.
- [x] Validate CPU/GPU parity and benchmark transfer plus compute time.
- [x] Evaluate lighting visibility/ray batches using profiling and correctness evidence (CPU retained; see GPU-LIGHTING.md).

Acceptance: a GPU executes compiler work on a real device; output is compared with
the CPU implementation using documented tolerances; fallback works without GPU
dependencies. Auto selection must account for setup costs. A GPU label alone is
not acceleration. Full lighting GPU parity is a separate gate, not implied by
accelerated minimaps.

## M5 — Native desktop workbench

- [x] Build a Qt 6 Widgets application alongside the standalone CLI.
- [x] Provide map/game paths, saved project settings, quality presets, and option inspection.
- [x] Run asynchronous build/decompile queues with progress, cancellation, and elapsed times.
- [x] Offer searchable logs, diagnostics, output locations, and report export.
- [x] Include decompilation controls, hardware/backend selection, and build history.
- [x] Add keyboard accessibility, DPI-aware layout, validation, and persistent preferences.
- [x] Test queue transitions, process errors, command construction, and project serialization.

Acceptance: users can configure, run, inspect, cancel, and repeat real CLI workflows
without shell commands. UI stays responsive; arguments are passed as an argument
array; build failures are visible. Headless tests do not inject mouse/keyboard
input. Any visual verification must respect the project's capture restrictions.

## M6 — Integration, packaging, and documentation

- [x] Verify optimized and CPU-only builds, CLI compatibility, and GUI workflows.
- [x] Package runtime dependencies with their licenses and installation instructions.
- [x] Document new options, recovery limits, measured gains, and remaining bottlenecks.
- [ ] Remove verified disposable task files; retain useful evidence in the project.
  Cleanup is blocked by automatic approval review, including a single explicitly
  named staging log. Temporary copies remain under `build/package/q3mapx-package-test*`
  and `.agents/tmp/`; source, delivery artifacts and benchmark evidence are retained.

Acceptance: reproducible release artifacts and clear evidence for implemented
features, with no unqualified claim of production readiness. Report unrelated
issues discovered during the work. Keep incomplete items visible in this plan.

## Execution order

Start with M0 and M1. Advance robustness and decompilation before using complex
files as optimization benchmarks. Implement scheduling before GPU integration.
Build the GUI against the stable CLI and reports, then complete packaging and
integration checks. Reorder individual tasks when evidence exposes a prerequisite,
recording that decision in the task log.

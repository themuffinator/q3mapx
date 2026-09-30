# Implementation plan

## Scope and working agreement

Create a standalone q3mapx compiler from the latest NRC source available at import
time. Retain the CLI, substantially improve performance and robustness, improve
BSP decompilation, and deliver a capable native GUI. Major rewrites are permitted
when measured results and correctness justify them. Commit after each completed
task. A milestone can contain several independently validated task commits.
As requested on 2026-09-30, keep development on `main` and push to `origin/main`
after each completed, validated round. Preserve existing history; no force pushes.

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

## M7 — Broader lighting performance and platform verification

The initial release is a tested baseline, not the end of the original optimization
scope. The continuing audit uses representative lighting/material fixtures and
actual Linux execution to close gaps that minimap benchmarks cannot establish.

- [x] Build and run the Linux release/GUI and sanitizer suites using available WSL.
- [x] Repair issues found by those checks and record exact platform evidence.
- [x] Add material-aware lighting parity fixtures and end-to-end benchmarks.
- [x] Reduce measured lighting costs while preserving shader/shadow semantics.
- [x] Implement and measure batched GPU lighting work (experimental area factors;
  complete-bake timings retain CPU as the default, with evidence in GPU-LIGHTING.md).
- [x] Refresh release artifacts and documentation after the additional task commits.

Acceptance: main lighting workloads have measured evidence and output checks;
GPU paths preserve supported material behavior or explicitly route it through
the CPU. Linux and sanitizer claims are backed by executed tests. No scope item
is treated as complete solely because it was deferred in an earlier document.

## Continuing work — game coverage, functionality and optimization

The next user-requested work uses the sibling fnTech3 repository as a format
reference. Its engine compatibility claims do not establish compiler support.
See [game coverage](GAME-COVERAGE.md) for the source revision, format distinctions
and validation requirements. Continue committing each completed task separately.

- [x] Audit the existing profiles against the reference and record the next work.
- [x] Optimize Raven lightgrid packing while preserving first-match output; repair
  dictionary exhaustion and validate boundary, malformed and real-command cases.
- [x] Publish a machine-readable game/capability catalog, accept useful fnTech3
  aliases, and use the catalog in the workbench rather than a partial static list.
- [x] Add BSP inspection with format identification, bounded parsing, actionable
  compatibility diagnostics and explicit ambiguity for shared file signatures.
- [x] Extend native BSP recovery to the missing fnTech3 format families, beginning
  with Alice/F.A.K.K.2, and cover Medal of Honor and early Quake III formats.
  Alice/F.A.K.K.2 recovery, FTX textures and unavailable-writer protection are
  implemented and validated, as are Medal of Honor terrain/placement recovery
  and the three early Quake III record formats.
  Preserve recoverable geometry/entities/material data and report format-specific
  losses. Native writing must not be advertised until its own contracts are met.
- [x] Exercise the existing writable game families through full synthetic compiler
  pipelines, decompilation and malformed inputs; add legitimate read-only map
  checks where available without redistributing proprietary assets.
- [x] Repair mesh-export gaps exposed by native-map checks: curved patches,
  brush-entity origins and ASE surface normals; expose mesh export in the GUI.
- [x] Measure further optimizations, run the relevant Windows/Linux/sanitizer
  checks, update user documentation and deliver refreshed binaries.

Delivered as 0.3.0, with the [integration matrix](validation/release-0.3.0.json)
and [portable artifact audit](releases/0.3.0-windows-x64.json). The earlier M6
staging-cleanup restriction remains separate and has not been bypassed.

Acceptance: actual format readers and workflow tests establish coverage; aliases
alone do not. Optimization reports compare identical inputs and output contracts,
include startup/serialization costs, and separate microbenchmarks from end-to-end
results. Unsupported workflows fail before modifying the input or creating a
misleading BSP. Existing CLI/GUI workflows remain available.

## Continuing development after 0.3.0

The requested continuing development retains performance, robustness, recovery,
game coverage and the workbench as active areas. These are the first priorities,
not a redefinition of the overall goal around the existing release:

- [x] Protect mesh exports against write/close failures and failed publication;
  stage OBJ/MTL together, roll back a published companion on a reported failure,
  and verify existing files survive actual filesystem errors.
- [x] Bring the bounded BSP inspector into the workbench with asynchronous
  discovery, useful directory/compatibility details and visible invalid-input
  diagnostics; preserve the CLI and test real compiler responses.
- [x] Extend checked publication to recovered MAP/report pairs after larger-map
  investigation exposed replacement before report failure; retain exact output
  bytes and verify rollback, buffered failures and legacy conversion.
- [x] Bound repeated validation of overlapping surface-index ranges, retaining
  local vertex limits, ignored unused values and the original first-error order;
  measure adversarial costs and check native-map compatibility.
- [x] Replace the raw workbench hardware query with a validated device table,
  bounded combined output, deadlines, cancellation and stale-reply protection;
  verify GPU-free replies and compiler changes on Windows and Linux.
- [x] Apply combined-output accounting to game-catalog discovery, including
  stderr-only/mixed overflow, exact boundaries and superseded child cleanup.
- [ ] Profile larger recovery workloads and address measured costs with geometry,
  UV/material and worker-count parity evidence before claiming speedups.
  Initial whole-command comparisons now cover a dense generated room and private
  native maps. Publication failures found during that work are repaired; finer
  reconstruction profiling and scheduling changes remain open.
- [ ] Continue native-format, input-validation and GUI improvements found by those
  checks, update the guides and validate the resulting release on Windows/Linux.
- [ ] Localize intermittent Windows process delays outside the measured VIS
  passes. They occur with both preceding and current executables at 20/70 workers;
  preserve complete-command timing evidence while investigating serial work,
  publication and worker shutdown separately.
- [ ] Make fatal BSP/SaveFile write failures clean staged files as well as protect
  the old destination. A Linux VIS file-size-limit test preserves the BSP/PRT but
  exposes the inherited `SafeWrite` exit path bypassing the staging destructor.

Each task must have its own implementation, validation evidence and commit.
Publication of several filesystem names cannot be advertised as one crash-atomic
operation; documented failure guarantees must match the actual implementation.
Keep remaining work visible as development proceeds.

## M8 — Decompiler inference and authoring fidelity

User-requested expansion, 2026-09-30. Shared evidence and initial optional
geometric detail/surface grouping policies are implemented; broader inference/review remains active.
Detailed design, source-data
limits and acceptance fixtures are in [recovery inference](RECOVERY-INFERENCE.md).

- [ ] Build shared BSP evidence/provenance analysis and a known-source evaluation
  corpus, with saved overrides and a versioned report extension.
  The first [evidence command](BSP-EVIDENCE.md) and structural/detail controls are
  implemented: native validation, hashes, ownership, local partition associations,
  regional costs and stored PVS statistics. Saved proposals/overrides, broader
  inference fixtures and an authoring review workflow remain open.
- [ ] Infer detail versus structural geometry using BSP partitions, leaf/cluster
  relationships, VIS/PVS and compatible portal evidence; validate classifications
  by recompilation, preserve seals/occluders and expose ambiguous cases.
  The initial known-source audit now covers 24 fast/full MAP round trips, exact
  brush geometry and spatial PVS comparisons. Fast export's dropped detail bits
  are repaired. An explicit CLI/workbench rebuild-order option now compensates
  for loader insertion, with 38 additional rebuilds covering opacity, entity origins and
  redundant sides. Projects retain the policy, and the workbench checks advertised
  compiler/profile support before queuing. Discarded contradictory side flags
  and brush-model detail ambiguity remain explicit findings for the inference work.
  The CLI now offers bounded convex brush-interior evidence and an opt-in cell
  policy with current-material protection and per-brush decision provenance.
  Thirty-six additional rebuilds cover missing detail references, non-first-side
  structural semantics and positive/negative controls. Complete leaf adjacency,
  PRT integration, wider calibration, saved overrides and GUI review remain open.
  The workbench now saves independent detail/group policies and their budgets,
  checks advertised support and rejects incompatible jobs before staging. Known
  assemblies exercise actual queue export; compact/full light/dark window checks
  cover saved settings, old compiler fallback and accessible controls.
- [ ] Infer useful `func_group` membership from geometry, materials, repeated
  assemblies and compile-property evidence, independently of detail classification.
  Preserve entity ownership and distinguish inferred groups from surviving metadata.
  The initial CLI/workbench surface-association policy now proposes and exports bounded
  world assemblies, copies recovered baseline compile settings, preserves brush
  insertion/collapse order and reports exclusions and ambiguity. Forty-two rebuilds,
  three lighting controls and 6,000 generated ordering layouts cover this policy.
  Repeated/disconnected assemblies, original parameter inference, alternatives,
  saved overrides, broader calibration and workbench review remain open.
- [ ] Infer stripped entity lights from baked lightmaps, vertex/grid and available
  directional lighting. Explain sky/sun, surface emitters, ambient and indirect
  contributions before fitting residual point/spot lights; account for bake settings.
- [ ] Infer spotlight position, aim, cone and falloff; find compatible surviving
  targets and connect them appropriately, or propose clearly labelled replacement
  target entities without disturbing existing gameplay links.
- [ ] Improve multi-triangle UV fitting, compatible brush-fragment reconstruction,
  plane/grid recovery, patches/model instances, hidden faces and entity relationships.
  Fast nonaxial plane output now avoids float-basis/decimal truncation, with exact
  stored-plane rebuild controls across six dominant-axis/sign orientations.
- [ ] Add CLI/workbench review, confidence/provenance overlays, manual corrections
  and iterative rebuild comparisons for geometry, collision, visibility and lighting.

Acceptance: known-source tests separate extraction fidelity, inference accuracy
and rebuild similarity. Stripped-light fixtures include sun-only/emissive-only
negative cases, mixed lighting and missing assets; target-link tests prevent
unrelated retargeting. Missing or ambiguous evidence remains explicit. An exact
original MAP cannot be promised when compilation discarded its distinguishing
information; the objective is the closest supported, editable recreation.

## M9 — Intelligent visibility and portal optimization

Separate compiler workstream; see [compiler optimization design](COMPILER-OPTIMIZATION.md).

- [x] Audit the existing VIS merge foundation: retain directional hint/sky flags,
  bound leaf/winding unions, repair convex joins, preserve PRT on output failure,
  and compact live working bits while retaining deterministic job order. Compare
  the repaired uncompressed solver and default/merge/hint modes on matched
  structural/manual-detail inputs. This does not complete the intelligent pass.
- [ ] Diagnose regional over-portalling, inefficient splits and poor detail usage,
  attributing costs to source geometry and comparing current merge/hint options.
  `-bsp-evidence` now ranks subtree subdivision/reference costs for investigation;
  matched merge/hint baselines now exist; regional portal adjacency/provenance
  and actionable inefficiency decisions remain open.
- [ ] Add a VIS-only option for conservative graph simplification with correct
  cluster mapping and bounded increases in runtime visibility work.
- [ ] Add a coordinated full-build option that examines regional structure and
  trial-rebuilds justified detail/splitter changes before generating BSP/PRT/VIS.
  Retain sealing, occlusion, area portals, explicit constraints and collision.
- [ ] Expose automatic validated application, exclusions, explanations and fallback
  in CLI/workbench; preserve the source MAP and regenerate affected downstream data.
- [ ] Measure total build time, memory, portals/clusters and potentially visible
  surfaces/triangles on badly detailed maps and deliberately difficult controls.

Acceptance: reduce measured inefficiency without false culling, broken connectivity
or leaks. Portal count alone is insufficient: a compile-time win cannot silently
cause unacceptable runtime overdraw. VIS runs after portal generation, so source
reclassification requires an explicit BSP rebuild. Baseline visibility inclusion,
geometric invariants and spatial correspondence across rebuilt trees form the
correctness checks; screenshot/ray samples alone do not establish safety.

## M10 — Geometry optimization without presentation changes

Separate compiler workstream; see [compiler optimization design](COMPILER-OPTIMIZATION.md).

- [ ] Analyze regional triangle costs and safe reduction opportunities relative
  to existing meta-surface processing; preserve source-to-output provenance.
- [ ] Remove proven redundant geometry and retriangulate compatible planar regions
  while preserving coverage, interpolation, materials, lightmaps, seams and normals.
- [ ] Investigate redundant patch tessellation with correct LOD/stitching, retaining
  curved silhouettes and all topology-sensitive shader behavior.
- [ ] Protect collision, contents, visibility, entity ownership and native format
  contracts. Evaluate compiled-BSP rewriting separately from the source-build pass.
- [ ] Provide reports, regional exclusions, CPU/job determinism and visual comparison
  tools; reject transformations whose presentation equivalence cannot be established.

Acceptance: fewer rendered triangles with preserved appearance and behavior, tested
with difficult UV/lighting/material cases, multiple views/distances and animation
states. Cache/index reordering is reported separately from triangle reduction.
Approximate decimation does not satisfy this preservation requirement. Engine
render validation uses windowed operation and registered render-target screenshots.

## Next execution sequence

Retain the open robustness and recovery-performance tasks above. Begin the new
workstreams with shared evidence extraction, known-source fixtures and comparison
tools. Advance detail/group inference and regional VIS/triangle diagnostics next.
Develop light fitting from surface/sky explanations through point lights and then
spotlight/target recovery. Gate automatic compiler edits on their independent
visibility or presentation checks. Each completed implementation, validation and
documentation task gets its own commit; these additions do not mark any new
feature as already delivered or narrow the broader continuing development goal.

## Initial execution order

Start with M0 and M1. Advance robustness and decompilation before using complex
files as optimization benchmarks. Implement scheduling before GPU integration.
Build the GUI against the stable CLI and reports, then complete packaging and
integration checks. Reorder individual tasks when evidence exposes a prerequisite,
recording that decision in the task log.

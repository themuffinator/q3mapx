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
- [x] Stabilize meta-surface ordering independently of shader heap addresses and
  initialize MAP patch channels before interpolation/publication. Check unused,
  reordered and duplicate shader definitions, worker parity, native IBSP/RBSP
  patches and complete compile pipelines; preserve geometry/ownership semantics.
  See [compiler reproducibility](PERFORMANCE.md#stable-meta-surfaces-and-patch-data).
- [x] Validate MAP patch dimensions before conversion/allocation, parse complete
  finite numeric tokens, reject incomplete matrices and preserve previous outputs
  on parse failure. Malformed-source tests also exposed and repaired quoted-EOF
  reads, token-terminator writes and quoted-NUL bypasses in the shared tokenizer.
  See [MAP patch checks](MAP-INPUT.md) for accepted inputs and validation limits.
- [x] Reject empty/incomplete MAP entities and brush structures, preserve quoted
  entity data, bound iterative include expansion and disable includes in BSP
  entity text. Validate native syntax, include limits and previous-output safety;
  see [MAP and script checks](MAP-INPUT.md).
- [x] Validate brush plane points, texture matrices/parameters and legacy flags;
  guard degenerate Quake sides before texture projection, bound derived numeric
  results and hash large finite distances safely. Check valid IBSP/RBSP lump
  parity, malformed inputs and previous-output preservation; see
  [brush input checks](MAP-INPUT.md#brushes).
- [ ] Continue the source-parser audit with optional-token lookahead across
  includes and multiline quoted-token line accounting. Strict primitive parsing
  does not make every shared script consumer strict.
- [x] Reproduce and repair malformed leading signs in the shared CLI numeric
  helpers. `+-1`/`+-0` now fail instead of being normalized into valid values;
  native option tests cover preserved outputs and unchanged valid signed,
  decimal/scientific and integer-limit behavior. See [numeric input](CLI-INPUT.md).
- [ ] Audit the separate legacy positional BSP scale/shift parsers, which still
  use `atof`; retain valid numeric/vector semantics and protect existing BSPs.
- [ ] Repair repeated surface classification reapplying a shader's absolute
  sample size after consuming entity lightmap scale. The initial M11 authored
  override bypasses that inherited issue; legacy semantics remain unchanged.
- [ ] Remove LIGHT-only MAP brush parsing's dependency on the first shader's
  initialized texture dimensions. Delaying SRF shader resolution exposed a
  division by zero in derived mapping; this round restores resolution before
  MAP parsing, while the inherited first-shader assumption remains to be fixed.
- [ ] Profile larger recovery workloads and address measured costs with geometry,
  UV/material and worker-count parity evidence before claiming speedups.
  Initial whole-command comparisons now cover a dense generated room and private
  native maps. Publication failures found during that work are repaired; finer
  reconstruction profiling and scheduling changes remain open.
- [ ] Continue native-format, input-validation and GUI improvements found by those
  checks, update the guides and validate the resulting release on Windows/Linux.
  Remaining raw sidecar writers and fixed-size lightmap path-format warnings
  need their own audit; checked BSP/SaveFile publication does not cover them.
- [ ] Localize intermittent Windows process delays outside the measured VIS
  passes. They occur with both preceding and current executables at 20/70 workers;
  preserve complete-command timing evidence while investigating serial work,
  publication and worker shutdown separately.
- [x] Make reported BSP/SaveFile write failures clean staged files as well as
  protect the old destination. Both native serializers and shared buffer saves
  now use owned, checked streams; failures unwind before the fatal diagnostic.
  OS write-limit, sharing-lock, successful retry and output parity checks cover
  this path. See [output safety](OUTPUT-SAFETY.md).

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
  structural semantics and positive/negative controls. Optional
  [bounded leaf-path adjacency](CELL-ADJACENCY.md) now reconstructs geometric
  interfaces without the original PRT. Enclosure/degeneracy limits, original
  portal metadata and author causality remain explicit. Complete correspondence,
  inference integration, wider calibration, saved overrides and GUI review remain open.
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
- [x] Extract bounded native IBSP/RBSP baked-lighting observations with per-style
  internal atlas/vertex/control/grid provenance, geometric texel associations,
  explicit ambiguity, deterministic jobs and output-preserving limits. See
  [the extraction contract](LIGHTING-EVIDENCE.md). Colors remain encoded;
  external/deluxe data and bake calibration remain.
- [x] Associate curved-patch lightmap texels through bounded tensor UV inversion,
  with folded/singular mappings and boundary uncertainty exposed. Export constant
  UV primitive regions with representative geometry and correlated internal texel
  footprints. Preserve stored controls and distinguish their normal field from
  geometric normals; neither is asserted to recover the original bake or runtime
  tessellation. Alternate-root review and broader bake/renderer qualification remain.
- [x] Add a read-only direct-light CPU reference using real compiler source
  creation, attenuation and material tracing, with proposed point/spot/sun lights,
  per-source/style provenance, bounded jobs and protected input/report files.
  [Direct lighting probes](LIGHT-PROBES.md) now provide this prerequisite;
  byte-transfer calibration, complete bake effects and inverse fitting remain open.
- [x] Connect geometric internal-lightmap observations to the CPU reference and
  shared native encoding, with explicit sampling/ambient assumptions, exclusions,
  per-style byte residuals and bounded automatic selection. Validate actual bakes,
  known supplied proposals and wrong-encoding/position controls. This evaluates
  hypotheses; unknown bake calibration and candidate generation remain open.
- [ ] Infer stripped entity lights from baked lightmaps, vertex/grid and available
  directional lighting. Explain sky/sun, surface emitters, ambient and indirect
  contributions before fitting residual point/spot lights; account for bake settings.
  Initial [conditional point fitting](POINT-FITTING.md) now searches BSP space and
  refines nonnegative point colors/intensities/positions using native transport,
  fixed retained/material/sky sources and withheld texel blocks. Blind synthetic
  localization, overlapping sources and full recovered-map lighting rebuilds
  provide initial qualification. Qualified fits can now be exported as entities
  through the CLI and the workbench's explicit report selection. Unknown
  encoding/bake calibration, indirect contributions and broader real-map accuracy remain.
- [ ] Infer spotlight position, aim, cone and falloff; find compatible surviving
  targets and connect them appropriately, or propose clearly labelled replacement
  target entities without disturbing existing gameplay links.
  Initial [conditional spotlight fitting](SPOT-FITTING.md) now searches native
  positions/directions/cones and nonnegative colors/intensities, with fixed native
  falloff, joint refinement and separate illuminated-support qualification.
  It proposes compatible surviving static marker links or uniquely named new
  markers. [Explicit report application](LIGHT-RECOVERY.md) now exports qualified
  fits and their fixed-light dependencies during decompilation, preserving target
  relationships and recording provenance. Typed workbench controls now run native
  fitting jobs and show score, source and target details before selecting a report
  for MAP export. Custom falloff, spatial overlays and broad
  real-map/target-identity qualification remain open.
- [ ] Improve multi-triangle UV fitting, compatible brush-fragment reconstruction,
  plane/grid recovery, patches/model instances, hidden faces and entity relationships.
  [Multi-triangle UV consensus](UV-RECOVERY.md) is now implemented in the native
  exporter: preserve already-supported transforms, fit agreeing evidence, report
  conflicts/limits and retain a compatibility policy. Independent numeric oracles
  and native world/entity rebuilds cover the initial affine recovery contract.
  Consensus output now preserves recovered whole texture offsets and small
  parameters, with precise patch control serialization and native absolute-UV
  rebuild tests. Constant axes, extreme arithmetic, broader renderer/editor
  qualification and geometric/chart reconstruction remain open.
  Fast nonaxial plane output now avoids float-basis/decimal truncation, with exact
  stored-plane rebuild controls across six dominant-axis/sign orientations.
- [ ] Add CLI/workbench review, confidence/provenance overlays, manual corrections
  and iterative rebuild comparisons for geometry, collision, visibility and lighting.
  Initial [light recovery review](WORKBENCH.md#light-recovery) now includes bounded
  background report parsing, BSP/game checks, training/withheld scores, proposal
  details, recorded settings and exact report staging. Native queue export and
  independent rebakes cover point/spot and supplied-light sRGB scenes. Spatial
  overlays, interactive edits and a general iterative comparison workflow remain.

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
- [x] Reduce passage construction work and retained storage without changing
  graph/solver results: intersect flood bounds before geometric tests, skip empty
  candidate sets, pack word spans into one block per source portal and free them
  after flow. Compare exact VIS bytes with the preceding dense implementation.
- [x] Audit passage clipping above 24 winding points. Replace truncation with
  complete-winding clipping and bounded intermediate growth, retain input on
  capacity exhaustion, and accept exactly 512 cached separators. Validate
  rotated/reversed large-portal visibility against analytic expectations and
  half-plane feasibility, plus matched 64-point native portals and ordinary-map
  reference parity. Recursive portal clipping still has a conservative small-
  buffer fallback; this does not complete the broader geometric/merge audit.
- [x] Reconstruct bounded world-path cells and their coplanar interfaces from BSP
  planes/tree, independently of stored leaf bounds, PVS and PRT. Expose enclosure,
  numerical and work/memory limits. Check independent box/oblique oracles, matched
  compiled portals and native recovery-only readers. This geometric prerequisite
  does not recover protected PRT flags or authorize VIS substitution.
- [ ] Diagnose regional over-portalling, inefficient splits and poor detail usage,
  attributing costs to source geometry and comparing current merge/hint options.
  `-bsp-evidence` now ranks subtree subdivision/reference costs for investigation;
  matched merge/hint baselines now exist. Its optional `-portals` analysis now
  maps graph costs to regions, reports protected/bridge/shape observations,
  samples local world-brush associations and counts stored-PVS world triangles.
  Independent graph/shape/PVS oracles and matched source-detail controls cover
  these diagnostics. Proven portal causality, validated inefficiency decisions,
  workbench overlays and automatic rectification remain open.
  An isolation probe confirms that disabling polygon merging alone does not
  remove the baseline-inclusion failures; leaf merging needs its own correctness
  argument and validation gate before automatic selection.
  The complete-winding repair still leaves 2/27 omitted baseline bits on the
  grid=5/grid=9 combined-merge probes. Use reconstructed cell interfaces for
  bounded geometric correspondence/coverage checks before proposing transformations;
  original PRT construction and serialized BSP planes have measurable oblique
  differences, so successful point probes are insufficient.
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

- [x] Implement a bounded, deterministic planar vertex-removal core with exact
  topology/interpolant predicates, replayable edits and independent rational
  oracles; measure read-only candidates on compiled, baked synthetic grids.
  See [core contract and evidence](PLANAR-REDUCTION.md). This prerequisite is not
  by itself a claim of renderer equivalence; the native adapter has separate gates.
- [x] Deliver an initial post-LIGHT IBSP46 adapter, bounded shader inventory,
  deterministic surface jobs, shared-index allocation, checked BSP/report output
  and CLI/workbench workflows for explicit Quake3e OpenGL use. Qualify authored
  nomarks/nodlight horizontal world surfaces through independent byte/topology
  checks and engine render targets. See [the implemented scope](GEOMETRY-OPTIMIZATION.md).
- [ ] Analyze regional triangle costs and safe reduction opportunities relative
  to existing meta-surface processing; preserve source-to-output provenance.
- [ ] Add native adapters and conservative renderer/material eligibility, including
  dynamic-light, fog, deformation and vertex-color quantization constraints.
  Compare pre-LIGHT and post-LIGHT application before choosing the initial pass.
  The first adapter chooses post-LIGHT so final quantized colors are known;
  unsupported behavior is protected. A dynamic-light qualification failure
  narrowed the initial contract without changing its raster tolerance. Other
  orientations, materials, renderers and native formats remain open.
- [ ] Remove proven redundant geometry and retriangulate compatible planar regions
  while preserving coverage, interpolation, materials, lightmaps, seams and normals.
- [ ] Investigate redundant patch tessellation with correct LOD/stitching, retaining
  curved silhouettes and all topology-sensitive shader behavior.
- [ ] Protect collision, contents, visibility, entity ownership and native format
  contracts. Evaluate compiled-BSP rewriting separately from the source-build pass.
- [ ] Provide reports, regional exclusions, CPU/job determinism and visual comparison
  tools; reject transformations whose presentation equivalence cannot be established.
  Surface reports, exact shader/surface exclusions, one/four-worker parity and an
  optional fixed-camera Quake3e harness are delivered for the initial adapter.
  Workbench region overlays/exclusions, broader native/hardware qualification and
  performance measurements remain open.

Acceptance: fewer rendered triangles with preserved appearance and behavior, tested
with difficult UV/lighting/material cases, multiple views/distances and animation
states. Cache/index reordering is reported separately from triangle reduction.
Approximate decimation does not satisfy this preservation requirement. Engine
render validation uses windowed operation and registered render-target screenshots.

## M11 — Radiant painting and per-surface lighting controls

User-requested expansion, 2026-09-30. Compiler support and companion Radiant
integration are both required for completion. The initial density implementation
uses a maintained NRC core patch because persistence, undo and rendering need
editor model hooks. See [the implemented contract](RADIANT-AUTHORING.md).
Native paint tools and source persistence are implemented experimentally. The
broader material-preview/runtime qualification requirements remain open.

### Patch vertex RGB and alpha painting

- [ ] Audit inherited NRC patch resizing: `setDims` adjusts `m_height` instead of
  its even-height argument, and `RemovePoints` advances the source pointer using
  the destination row stride. Painted input requires odd dimensions and painted
  row reduction is guarded; broader legacy operations need targeted checks.

- [x] Add independently editable per-vertex RGB and alpha on patches, without
  requiring `alphaMod` brushes. Define persistent source metadata and its mapping
  from authored patch vertices to tessellated vertices; preserve unpainted defaults.
  The experimental compiler foundation now accepts `q3mapxPatchDef2` with RGBA
  controls, explicit lighting/material RGB modes and bounded uniform tessellation.
  Painted patches render as triangles to retain gradients on flat geometry;
  legacy patches remain unchanged. Native control storage, MAP/XML transfer,
  duplication and undo now retain the fields; module API version 2 requires a
  matching editor/module rebuild.
  See [the source/lighting contract](PATCH-PAINT.md).
- [x] Add Radiant painting with separate RGB/alpha channel controls, color picking,
  brush size/strength/falloff, fill/reset, selection masks and undo/redo.
  A native parameter-space canvas previews raw Bezier RGBA over a checker;
  strokes use bounded coverage and commit as one undo operation after checking
  the source snapshot. Native Windows model/action checks and saved-map compiler
  round trips pass. Interactive mouse ergonomics still need qualification.
- [ ] Add a material preview which evaluates supported shaders' RGB/alpha stages,
  blending/depth and lighting, with camera/runtime render-target comparisons.
  The current paint canvas does not evaluate shaders or camera-rendered paint.
- [ ] Define how authored color/alpha interacts with lighting, shader modifiers
  and native BSP color/light-style channels. Preserve the intended result through
  patch subdivision, tessellation, LOD stitching, merging and geometry optimization;
  keep seams and deliberately discontinuous painted regions intact.
  Initial Q3/JA compiler tests cover flat/curved sample fields, material/lighting
  separation, density, both meta vertex-merging passes, shader modifiers and
  direct/bounced/repeated LIGHT. BSP/SRF binding rejects stale paint metadata.
  Native RGB styles, runtime material/LOD seam equivalence, large-map costs and
  paint-aware reduction remain open; finite tessellation is not pixel-exact.
- [ ] Preserve source paint through save/reload, duplication and patch editing.
  Recover compiled colors during decompilation, and restore original paint only
  where retained authoring metadata supports it; distinguish paint from baked light.
  Decompilation now warns about unrecovered authored paint when its BSP marker
  is present. The build binding is not a source-control archive. Native
  save/reopen, duplication, undo/redo, transpose/inversion and bounded insertion
  now retain paint. Insertion rounds new controls to bytes; lossy row reduction
  and caps are guarded. Wall generation, third-party topology plugins, exact
  paint-aware reduction and decompiler paint recovery still need work.

Acceptance: paint RGB and alpha independently in Radiant, save/reopen the map and
compile it through the CLI with the same intended material appearance, without
auxiliary alpha-modifying brushes. Validate gradients, transparent boundaries,
adjacent patches, tessellation/LOD changes, lit and unlit materials, supported
native formats and one/multiple workers. Preview and runtime comparisons must
state the supported shader/rendering contract.

### Per-surface lightmap density with preview

- [x] Add persistent lightmap-density overrides for individual brush faces and
  patch surfaces, editable in Radiant without splitting entities or cloning
  shaders. Define units, scaling and precedence against global, entity and shader
  settings, with clear inheritance and reset-to-default behavior.
  Delivered for Quake III axial/BP/Valve 220 faces and patchDef2: versioned inline
  primitives, native Surface Inspector actions, copy/memento/MAP/XML persistence
  and CLI propagation. The Windows editor is experimental; Linux editor delivery,
  topology-changing operations and a distributable companion package remain open.
- [ ] Provide interactive texel-grid/checker and density overlays in Radiant's
  viewport, including curved patches. Show effective sampling density and an
  estimated lightmap cost; identify unsupported/unlightmapped surfaces and any
  native-format limits or clamping. Distinguish estimates from actual baked atlas
  placement and sample counts, and allow inspection of the compiled result.
  A bounded requested-spacing grid for face polygons and curved tessellation,
  area-based estimates and mixed-selection feedback are implemented. Native grid,
  draw-submission and widget tests pass; camera raster, material eligibility,
  effective inherited settings and compiled-atlas inspection are still required.
- [ ] Carry each authored surface's override through BSP splitting, patch
  tessellation, meta merging and lightmap allocation. Prevent merges from silently
  erasing different density settings, and report effective values in CLI/workbench
  diagnostics. Preserve settings through editor save/reload and duplication.
  The initial compiler adapter retains authored values in sides/patches/surface
  copies/meta triangles and exposes authored/effective sizes in SRF. Distinct
  values stay separate even when clamped to equal spacing. Workbench diagnostic
  presentation and broader split/native-format qualification remain open.
- [ ] Validate mixed densities on adjacent coplanar faces and curved patches,
  entity transforms, packing limits, inherited settings and legacy MAP input.
  Check preview predictions against compiled sampling/atlas data, lighting seams,
  memory/compile cost and worker determinism.
  Windows/Linux Release and Debug ASan/UBSan pass the initial Quake III/JA matrix,
  malformed-source safety, legacy parity, clamp/vertex-lit checks and actual baked
  UV footprints. Preview raster, seams, large-map costs and other profiles remain
  outside that evidence. See [validation](validation/radiant-density.json).

Acceptance: changing a selected surface's density updates its Radiant preview
and affects that surface's effective compile setting while other surfaces retain
their settings. Source settings survive save/reload and the compile workflow;
maps without overrides retain existing results. A shared atlas may be repacked,
so unrelated atlas coordinates are not promised to remain byte-identical.

Shared delivery requirement: use versioned, validated source metadata with stable
authoring associations rather than transient compiled surface numbers. Define
legacy editor/compiler compatibility and explicit diagnostics for unsupported or
lost metadata. Deliver the compiler/CLI contract, Radiant integration, fixtures
and documentation together; preserve the existing headless workflow.

## Next execution sequence

Retain the open robustness and recovery-performance tasks above. Begin the new
workstreams with shared evidence extraction, known-source fixtures and comparison
tools. Advance detail/group inference and regional VIS/triangle diagnostics next.
Develop light fitting from surface/sky explanations through point lights and then
spotlight/target recovery. Gate automatic compiler edits on their independent
visibility or presentation checks. For M11, establish source metadata and native
format semantics first, then implement compiler propagation, Radiant tools and
preview validation. These authoring additions retain the existing recovery and
optimization priorities. Each completed implementation, validation and
documentation task gets its own commit; these additions do not mark any new
feature as already delivered or narrow the broader continuing development goal.

## Initial execution order

Start with M0 and M1. Advance robustness and decompilation before using complex
files as optimization benchmarks. Implement scheduling before GPU integration.
Build the GUI against the stable CLI and reports, then complete packaging and
integration checks. Reorder individual tasks when evidence exposes a prerequisite,
recording that decision in the task log.

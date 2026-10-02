# Development and validation

## BSP subdivision and depth

`bsp_tree` independently enumerates block boundaries and verifies graph path
lengths, including shuffled/shared nodes. `bsp_depth` exercises native Q3/JA
generation through 4,096-block rooms, axis/worker changes, VIS/minimap/LIGHT,
malformed graph rejection and deep-hint failure preserving BSP/SRF files.
Its optional `--reference` argument additionally checks preceding-compiler cells,
rendered meshes, mapped visibility and ordinary native bytes. See
[the contract and reproduction commands](BSP-TREES.md).

## Build policy

Requires CMake 3.25+, Ninja, a C++20 compiler, pkg-config, GLib, libxml2, Assimp,
libpng, libjpeg, and zlib. Qt 6.4+ Core/Gui/Widgets builds the desktop workbench. No game
assets are needed to build the compiler. Windows GCC/MinGW and Linux GCC are
validated toolchains; MSVC is not yet validated against inherited constructs.

On Windows, install the following from an MSYS2 MINGW64 shell if not already present:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf mingw-w64-x86_64-glib2 \
  mingw-w64-x86_64-libxml2 mingw-w64-x86_64-assimp mingw-w64-x86_64-libpng \
  mingw-w64-x86_64-libjpeg-turbo mingw-w64-x86_64-zlib mingw-w64-x86_64-qt6-base
```

Then, from the repository in PowerShell (adjust the MSYS2 path if needed):

```powershell
$env:PATH = 'C:\msys64\mingw64\bin;' + $env:PATH
cmake --preset release
cmake --build --preset release --parallel 8
ctest --preset release
.\build\release\bin\q3mapx.exe -help
```

The MSYS2 DLL directory must remain on PATH when running unbundled development
executables. The project does not change persistent environment settings.

On Debian/Ubuntu, install `build-essential cmake ninja-build pkg-config libglib2.0-dev
libxml2-dev libassimp-dev libpng-dev libjpeg-dev zlib1g-dev qt6-base-dev` and use the same CMake
commands. The executable is `build/release/bin/q3mapx`.

The GUI executable is `build/release/bin/q3mapx-workbench`. Use
the `cli` configure/build/test preset (or `-DQ3MAPX_BUILD_GUI=OFF`) for a Qt-free
CLI-only build. See [workbench usage](WORKBENCH.md) and [packaging](RELEASE.md).

`debug` and `profile` presets provide debug and optimized-with-symbols builds.
Optional `-DQ3MAPX_ENABLE_LTO=ON` enables release IPO after a compiler capability
check; `-DQ3MAPX_ENABLE_SANITIZERS=ON` enables ASan/UBSan on supporting GCC/Clang
toolchains, including support libraries and test targets. The `sanitized` preset
targets Linux GCC/Clang, disables GUI/GPU code and fails on sanitizer diagnostics.
Its leak detector is disabled because the inherited CLI intentionally retains many
process-lifetime compiler allocations; address and undefined-behavior checks stay
enabled. MinGW/MSVC sanitizer configurations are rejected explicitly. Baseline
comparisons use LTO off and no fast-math. The sanitizer suite is also executed
locally under Ubuntu 24.04 WSL; local results do not imply a remote CI run.
The Debug sanitizer preset uses `-g -O1` for C/C++ so complete native fitting
searches remain practical under instrumentation. Debug assertions remain enabled;
this does not add `NDEBUG`, disable sanitizer checks or enable fast-math.

OpenCL support is enabled by default but dynamically loads the installed GPU
driver only for compute/device queries. No OpenCL SDK is required to build.
`cmake --preset cpu-only`, `cmake --build --preset cpu-only`, and
`ctest --preset cpu-only` exercise a build with GPU code disabled.
`Q3MAPX_DISABLE_GPU=1` disables runtime loading for fallback testing.
Hardware tests use available native OpenCL GPUs and report when none are present.

Do not require developer-specific absolute dependency paths in portable project
configuration; local paths belong in environment variables or ignored
`CMakeUserPresets.json`.

Build outputs belong under `build/`. Disposable scripts, logs, downloaded inputs,
and test runs belong under `.agents/tmp/<task>/` unless a committed preset or test
harness designates another project-local build output directory. Never build or
write test output into a real game installation or reference asset directory.

## Correctness checks

1. Configure and build the actual executable.
2. Exercise help, game listing, and invalid arguments.
3. Compile a sealed synthetic map through BSP, VIS, and LIGHT.
4. Decompile and recompile fixtures; compare semantic invariants.
5. Test malformed BSP lengths and cross-references.
6. Check scheduling and GPU parity at varied thread/backend settings.
7. Check GUI project serialization, command arguments, queues, and process failures.

`index_validation` compares first invalid references with an independent ordered
scan over exhaustive boundary slices and seeded random overlaps. It also verifies
bounded source visits and scratch storage, so the performance contract does not
depend on a brittle wall-clock assertion. `index_validation_cli` checks the real
native loader with repeated/shifted spans, different per-surface vertex counts,
negative/overflowing indices, unused bad entries, empty slices and forced loads.
The earlier binary passes the same acceptance/diagnostic cases; the new helper
removes repeated work without changing those contracts. The
[benchmark guide](PERFORMANCE.md#shared-bsp-index-validation) covers timing evidence.

`vis_clip` compares passage clipping against an independent 2D half-plane
feasibility oracle, excluding cases within its numerical margin. It covers
24/25- and 511/512-point boundaries, cyclic/reversed winding order, intermediate
growth beyond 512 points, double-precision epsilon thresholds, exact capacity,
conservative overflow and scratch reuse. `vis_passage_clipping` checks 288
analytic graph runs at one/four workers in full, passage-only and portal-only
modes, plus six VIS runs on a matched BSP/PRT corridor with 64-point portals.
Its optional `--reference` records old wrong visibility and separator-limit errors;
the old executable is not the correctness oracle for repaired cases. Native
non-VIS lumps and the PRT must survive every successful run unchanged. These
checks complement ordinary-map reference parity in `vis_merge`.

`lighting_materials` generates alpha textures, colored filters, an emissive panel,
sun/sky, a curved patch and a brush model with an origin. It checks adaptive,
bounced, deluxe and supersampled lighting at one and four workers. Mutating alpha
and RGB texels independently verifies the material paths actually affect the bake.
It also detects unstable lightmap packing, which previously depended on shader
allocation addresses. Surface ordering now uses shader names and stable indices.
The fixture also exercises randomized supersampling, dirt, low-quality floodlight,
and dense-grid escape sampling. Dense bakes compare all lighting lumps and culling
statistics at 1, 4, 20 and 70 workers, including bounced grid light. Job-local random
streams and ordered bounce-light publication make these checks independent of
scheduling. This is repeatability within one build, not cross-platform floating-point
identity. Randomized output differs from the old shared C-library RNG sequence.

`area_factors` tests actual OpenCL polygon integrals on available native FP64
devices, including repeated batches, degenerate/large polygons and rejected
inputs. `lighting_gpu` compares hybrid/CPU baked data across material modes and
a dense streamed-cache case. Missing runtime/FP64 is an explicit hardware skip;
CPU-only configurations still verify that requested GPU startup fails without
modifying the input BSP. Kernel build/dispatch/parity failures are test failures.

Use explicit subprocess timeouts and capture stdout/stderr in test failure reports.
Do not introduce brittle tests that only mirror internal implementation details.
Do not launch a game fullscreen or control input. For any idTech rendering checks,
use only an engine-registered screenshot path; never substitute OS capture.

## Native Radiant authoring validation

The optional native NRC authoring build is separate from the compiler/workbench
CMake build. See [Radiant authoring](RADIANT-AUTHORING.md#prepare-and-build-the-companion-editor)
for the pinned patch, hash-checked preparation and native harness commands.
`surface_density` is part of ordinary CTest and exercises the real compiler
without an editor dependency. `tests/nrc_authoring.py` additionally requires the
patched Qt 5 editor harness and format modules; it writes only to a marked
project-local test profile, never an installed editor. The current editor tests
cover persistence, actions, widget layout and grid submission, not viewport raster
qualification. Patch RGB/alpha painting remains a separate M11 task.

`patch_paint` validates the [compiler RGBA contract](PATCH-PAINT.md) without the
editor. It covers analytic flat/curved samples, Q3/JA, both RGB modes, one/four
workers, repeated LIGHT and bounce saves, density, shader modifiers, shared-edge
colors and malformed-source/SRF output preservation. An optional `--reference`
checks legacy BSP/SRF and LIGHT output against the previous compiler. The native
editor does not yet read the new primitive. Compiler checks are prerequisites
for its painting UI, not evidence of editor or runtime raster completion.

## Performance evidence

Record compiler revision/build mode, CPU/GPU, worker count, input identity, exact
arguments, elapsed time, and output checksums or semantic comparison results.
Perform warmups and several measured runs. Report median and spread and separate
initialization from steady-state work where useful. Compare against the imported
baseline using the same toolchain and options. Keep deterministic fixture creation
scripts in `tests/` or `benchmarks/`; put generated large data in ignored outputs.

## Benchmark commands

For MAP recovery comparisons that require identical generated text and recovery
metadata across implementations and worker counts:

```sh
python benchmarks/recovery.py --baseline build/reference/bin/q3mapx --compiler build/release/bin/q3mapx --work-dir build/recovery-benchmark --grid 47 --threads 1 4 20 --repeat 5
```

Use q3mapx binaries with the same output-version header. The harness alternates
baseline/candidate runs after warmup, includes startup, assets and both output
files, and checks executable/input hashes before and after comparison. For private
native maps, add `--map /path/to/input.bsp --game ja --game-root /path/to/game`.
It copies the BSP into the work directory, uses installed assets read-only, and
records hashes/counts rather than native geometry in `benchmark.json`. `--format`
selects `map`, `map_bp` or `map_220`; `--fast` measures recovery without UV matching.
Keep timings separate from output-parity evidence and do not infer parallel
speedup merely from changing the requested worker count.

Run the repeatable benchmark from the repository root:

```sh
python benchmarks/compiler.py --compiler build/release/bin/q3mapx --work-dir build/release/benchmark --threads 1 4 --repeat 5
```

On Windows append `.exe` to the compiler path and keep the dependency DLL directory
on PATH. The harness generates its own dense room, preserves VIS portal input with
`-saveprt`, warms up each stage, and saves raw observations and arguments in
`benchmark.json`. [Initial results](benchmarks/baseline-win-x64.json) are a baseline,
not an optimization claim. CI definitions cover Windows/MinGW and Linux; a workflow
definition does not imply a remote run has passed.

For alternating complete lighting comparisons:

```sh
python benchmarks/lighting.py --baseline build/reference/bin/q3mapx --compiler build/release/bin/q3mapx --work-dir build/lighting-benchmark --grid 21 --threads 1 20 --repeat 5
```

Add `--backend gpu` to use GPU area factors in the candidate executable (the
baseline remains CPU). `--accurate` omits the fast light-envelope cutoff; it can
increase runtime substantially, so start with `--grid 9`. The harness restores
the unlit BSP before every bake, checks all lighting lumps and verifies that
neither executable changed during measurement. GPU reports are embedded in the
result so an accidentally unused backend cannot produce a speedup claim.

Enable `Q3MAPX_BUILD_BENCHMARKS` to build `scheduler_benchmark`. This compares the
old scheduler's mutex-per-item/fresh-thread algorithm with the new job pool using
tiny synthetic jobs; its speedup is not a compiler speedup.

## CPU jobs and profiles

`-threads auto` uses detected hardware concurrency. An explicit count must be in
`1..1024`; the old fixed 64-entry worker storage is gone. Workers persist between
passes, the caller participates, and job dispatch uses atomic range claims.
Uneven expensive passes use individual jobs; cheap grid/postprocessing passes use
bounded batches. Compiler data, scheduling, and progress have separate locks.
Nested jobs on the same pool run inline; exceptions return to the submitting
thread after all workers stop the failed pass. Fatal legacy errors terminate the
process; normal completion explicitly joins workers.

Add `-profile output.json` before any stage to save schema-versioned pass names,
item counts, actual worker counts, batch sizes, execution time, setup-inclusive
time, process time, and exit status. Profiles are currently written on normal
stage return, including nonzero returns; legacy fatal `Error()` exits do not write
one. The GUI must use the child process exit code as the authoritative result.

```sh
q3mapx -threads auto -profile light-profile.json -light -fast map.bsp
```

## Optional installed-map coverage

`ctest --test-dir build/release -R game_profiles --output-on-failure` exercises
every catalogued native writer through BSP, VIS, LIGHT, recovery, recompilation
and a minimap. It also checks aliases, native headers and unknown-profile output
protection. `game_catalog` checks the Qt catalog client; `workbench_window`
starts a complete build through the real window's action, offscreen without
mouse or keyboard events. Both GUI tests use isolated project-local state.
`workbench_devices` runs the real compiler's inventory and controlled child
processes for output limits, invalid replies, timeouts and cancellation. It
writes `native-devices.json` and `checks.json` under `tests/workbench-devices`
inside the build directory. The window test also uses this fixture executable
for repeatable device selection and cancellation, with direct widget painting
under `tests/workbench/window-state`; neither test needs a physical GPU.

The generated CTest fixtures remain independent of game installations. Additional
native-map evidence can be collected from user-owned PK3 archives:

```sh
python tests/native_maps.py --compiler build/release/bin/q3mapx --game ja --pak /path/to/base/assets0.pk3 --work-dir build/native-validation/ja
```

Repeat `--pak` for patch archives. Add `--map maps/example.bsp --decompile
--game-root /path/to/game` for recovery with installed textures read-only. The
harness uses one private staged input, logs, reports and outputs beneath the
chosen work directory; it never extracts archive paths into the filesystem or
writes to the installation. Reports contain names, hashes, counts and outcomes,
not proprietary assets. A BSP-validation pass is not a gameplay compatibility
claim. Source archives and compiler identity are checked for changes during each
probe.

Use repeatable `--bsp /path/to/loose.bsp` for loose IHV or other native maps.
`--obj` additionally exports and hashes the mesh, recording vertex/triangle
counts. Loose source paths and archive entries always become the same independent
private input name. Native regression groups are `native_fakk`, `native_mohaa`
and `native_early`; `mesh_export` verifies curved geometry, normals, entity
placement, worker parity, lightmap lookup and allocation/output boundaries.
`export_outputs` exercises actual companion-open errors, Windows sharing-lock
rollback/retry, POSIX file-size-limit write failures and link rejection. It checks
old output contents and source bytes, not only nonzero process status. These
fixtures write only to their designated test output directory.

`recovery_classification` builds original labelled Quake III fixtures and compares
all three MAP formats in fast/full mode. Its `tests/recovery-classification/validation.json`
contains source identities, classification expectations and spatial partition/PVS
differences. It compares visibility at corresponding world points instead of
assuming cluster indices survive rebuilding. Source/rebuild differences are
reported separately from required fast/full parity; this is a bounded generated
corpus, not a proof of general decompiler inference accuracy.

`recovery_order` checks 38 additional rebuilds with explicit loader-order
compensation. Five positive cases require original compiled brush order and
spatial relationships; a contradictory structural/detail control isolates lost
source flags. Redundant translucent sides, multiple brush-entity solids/origins,
default/legacy parity, worker parity and option/profile rejection are covered.
Evidence is written to `tests/recovery-order/validation.json` in each build tree.

`brush_cells` checks independently evaluated interior witnesses, analytical convex
volumes and 36 known-source rebuilds for optional cell detail inference. Removing
detail references must leave geometric evidence unchanged and still permit correct
fixture reconstruction. Material, VIS, geometry-limit and tree controls exercise
fallbacks; exact work budgets, worker parity, invalid options and existing-output
preservation cover failure paths. Six recovery-only readers and relocated early
world roots exercise read-only evidence. Each build records
`tests/brush-cells/validation.json`; these controls do not prove general authoring
fidelity or portal causality.

`recovery_group_order` independently simulates MAP brush insertion and group
collapse for 6,000 deterministic association/opacity/protection layouts, requiring
exact sequence preservation, unique ownership, stable decisions and budget checks.
`recovery_groups` adds 42 known-source rebuilds through fast/full and three formats,
including mixed detail/opacity, slopes, baseline compile parameters, original
entities and patches. Each checks exact ordered geometry/materials/contents and
spatial partition/PVS relationships at 650 points (422,500 ordered pairs). Three
full Valve 220 controls require identical baked lightmap/lightgrid bytes. Five
additional rotated fast-mode controls exercise the other dominant-plane-axis/sign
combinations without grouping enabled. Ambiguous flat-source and disconnected
order-blocked fixtures prevent treating plausible assemblies as original metadata.
World context, overlapping surface/model ownership, hard limits, option validation,
work budgets, worker parity, legacy conversion and previous-output preservation
have separate controls. Each build writes `tests/recovery-groups/validation.json`.
The [round's validation record](validation/recovery-groups.json) also compares the
preserved preceding executable: 39 default MAPs are byte-identical, while three
fast sloped outputs intentionally change and rebuild with corrected planes.

Workbench recovery checks cover saved/legacy brush-order settings, malformed
project/catalog fields, the real decompile job/report and run snapshot, and
absence of recovery arguments from other workflows. The offscreen window test
exercises compatible/native-only profiles, retained incompatible selections,
menu-action rejection before staging and direct QWidget rendering of the Recovery
tab at 1380×920 and 1024×720 in both themes. No OS capture or input injection is used.

The same workbench tests now cover optional detail/group policies and independent
work budgets, strict JSON validation and migration defaults. The real queue recovers
two labelled assemblies and verifies the compiler report and saved run snapshot.
Window actions check native and older-compiler rejection, retained choices,
automatic rebuild-order selection, saved budget edits, command preview, and no
directory creation on rejection. Compact/full renders cover collapsed/expanded
limits; explicit widget-height checks prevent dropdown text from disappearing
when the expanded section needs scrolling. Capability tests validate all three
optional policy arrays and grouping's rebuild-order dependency. See
[the validation record](validation/workbench-inference-controls.json).

`portal_graph` exercises bounded PRT syntax, complete records, finite coordinates,
indices, self-edges, winding/byte/point limits, every record truncation and a
seeded malformed corpus. Seeded decimal and scientific floats must have the
same binary32 values as the preceding `scanf` reader.

`cell_graph` independently compares generated box-tree face adjacency and areas,
with analytic oblique cuts, repeated/single leaves, thin/degenerate geometry, exact
capacity boundaries, malformed inputs and a 10,000-node iterative traversal.
`cell_adjacency` compares every reconstructed interface with matched compiled PRT
polygons and verifies its vertices against both native leaf paths. Quake 3/Raven
structural/detail pairs, a 64-sided corridor, all six recovery-only readers,
relocated/single-leaf roots, changed leaf bounds, worker parity, work exhaustion
and protected output have separate controls. The oblique fixture measures and
records the difference between float PRT construction and serialized BSP-plane
geometry under explicit comparison tolerances; it does not require false byte
equivalence. Reports remain identical before/after VIS and with/without a supplied
PRT. Each build writes `tests/cell-adjacency/validation.json`; `--grid 9` selects
larger axial controls. See [cell adjacency](CELL-ADJACENCY.md) for numerical limits
and the distinction between reconstruction and original portal recovery.

`portal_evidence` compares Quake 3 and Raven structural/manual-detail fixtures
before and after VIS. Independent oracles remove individual edges to find
bridges, traverse the BSP frontier, triangulate polygon fans and union world
surface IDs from stored PVS. Worker parity, shared nodes, 512 paths to one leaf,
padding, stale pairs, protected/unknown flags, bad winding geometry, missing VIS,
combined brush-cell analysis, work exhaustion and preserved report/input bytes
have separate controls. Each build writes `tests/portal-evidence/validation.json`;
pass `--grid N` to the Python harness for a larger generated control. These are
diagnostic checks, not validation of an automatic structural transformation.
See [regional diagnostics](PORTAL-ANALYSIS.md).

`vis_mask` checks 8,400 seeded packed/dense round trips and intersections, covering
empty/single-word masks, sparse/dense spans, word boundaries and the maximum
2,048-word mask. Poisoned reused output and guard words check zeroing and bounds.
The real `vis_merge` matrix additionally verifies passage storage/candidate
accounting, empty masks, allocation bounds and absence of passage construction
from the two solvers that do not use it.

`vis_passage_storage` exercises a 1,024-degree star (1,049,600 empty passage
descriptors) and a 130-opening straight chain crossing several mask words.
Independent PVS expectations, full/passage-only and one/four-worker parity,
native-lump/PRT preservation and storage counts cover eight controls. These are
synthetic graph fixtures on a native carrier, not spatially matched maps.

`vis_rows` compares 3,200 expansions with independent merge-parent walks and set
unions. It covers arbitrary representative ordering, sparse/dense/empty portal
masks, duplicate destinations, word-boundary padding, guarded output spans, two
16,384-deep forests and malformed parents/masks/groups. `vis_row_assembly` checks
24 native controls: a 2,048-cluster merged chain and seven disconnected groups
including an isolated cluster, under all four solvers and 1/4/20 workers. Exact
connected-component rows, geometry/PRT preservation, histogram totals and job
counts must match. These are synthetic graphs on native carrier BSPs; the
separate `create_subdivided_corridor` fixture supplies matched sealed geometry.
Pass `--reference` to the native Python harness to compare its two one-worker
full-solver cases with a preceding binary. See [row assembly](VIS.md#distinct-output-rows).

`vis_merge` exercises hint/sky directions, parallel openings, far-plane culling,
convex/concave/folded joins, winding and leaf capacity boundaries, complete
contraction and self-edge rejection. Real publication failures must preserve the
input BSP and PRT and leave no new staging file; a successful retry consumes the
PRT. Its matched generated
structural/manual-detail maps cover all four solvers and default/merge/mergeportals/
hint selections at 1/4/20 workers. `--workers` can select additional stress counts.
The merge matrix also checks `-nosort` without changing its job-slot ordering.
The graph-only fixtures intentionally isolate PRT contracts and are not claimed
spatial matches to their carrier BSP. All real-map checks retain non-entity,
non-VIS lumps and require worker-independent visibility bytes.

`vis_occlusion` checks the bounded rational interior-occlusion oracle with 1,274
independent polygon comparisons, 106 basic controls, 96 transformed/reversed
coupled-refinement cases and 80 continuous clear-ray controls. These include
tiny positive windows, axis permutations/reflections, large translations and
resource/certificate guards. It is Python-only and part of ordinary CTest.
`vis_merge_qualification` runs 64 native VIS cases per platform on known axial
source fixtures and independently replays generated certificates for dropped
baseline bits. All 84 distinct pairs now certify using exact coupled endpoint
and blocker constraints. This group is also ordinary CTest, with a 900-second
timeout; the standalone `tests/vis_merge_qualification.py` command remains
available. Any unresolved pair or failed replay fails qualification. It does
not change compiler behavior or qualify general geometry/engine boundary cases. See
[contract, results and reproduction](VIS-QUALIFICATION.md).

Pass `--reference /path/to/preceding/q3mapx` to require exact VIS bytes against
a compiler with the same merge repairs. The passage-storage round retains its
preceding `e2965d0` executables under `.agents/tmp/continuation/smart-vis-baseline`.
For the earlier live-bitset compaction comparison, use the original unpacked
layout reference; that round retains its executable and source identity locally.
The [reference patch](../benchmarks/references/vis-uncompressed-at-7aaa22b.patch)
recreates its source from `7aaa22b` in a separate test checkout. It intentionally
predates the PRT cleanup-order fix; comparisons keep the PRT in both executables.
`benchmarks/vis_portals.py` alternates those binaries with an optional preceding
legacy executable, records whole-process medians, requested passage storage and
per-cluster visible surface/triangle distributions. Existing merge-mode PVS
differences are recorded separately from required compaction parity; this is not
a renderer benchmark or validation of the planned intelligent transformation.

`benchmarks/vis_passages.py` compares the preceding dense passage layout with the
current implementation using alternating whole-command runs, warmup, all-cluster
VIS-byte parity and retained-storage counters. Its default matrix covers full
and passage-only solvers, ordinary/merged graphs, poor/manual-detail fixtures and
one/four workers. Per-pass times are separate from complete-command times;
retained passage bytes are separate from temporary reservations and process peak
memory. Use `--grid`, `--workers` and `--repeat` to select other measurement cases.

`binary_outputs` checks native IBSP/RBSP publication and shared profile saves
against real Windows sharing locks and POSIX file-size limits. Empty, ordinary
and large-lump inputs distinguish buffered header-seek and immediate write
failures. It requires original preservation, clean staging, successful retry,
directory/link rejection and no premature success message. Run it with
`--reference /path/to/previous/q3mapx` for full output-byte parity after masking
only the unused timestamp. `atomic_file` also checks 64-bit positions, invalid
operations and a real read-only stream error. See [output safety](OUTPUT-SAFETY.md)
for the covered writers and failure guarantees.

## Planar reduction validation

The `planar_reduction`, `planar_reduction_oracle` and `planar_reduction_native`
groups validate the geometry core, rational arithmetic/mesh parity and read-only
candidates on generated compiled grids respectively. Build `planar_reduction_test`
and `q3mapx`, then run `ctest --test-dir build/release -R '^planar_reduction' -V`.
Use the matching Linux build directory for Release or ASan/UBSan. Reports stay in
`build/<preset>/tests/planar-reduction*`. These tests do not establish runtime
shader eligibility or validate a BSP rewrite; see [the core guide](PLANAR-REDUCTION.md).

`quake3_materials` and `geometry_optimize` add bounded shader parsing, native
eligibility and index-only publication controls. The latter builds ambient and
point-lit grids, checks complete native preservation, one/four-worker parity,
shared-index allocation and transactional failures. `workbench`, `workbench_window`
and `game_catalog` cover the native queue paths and advertised capabilities.
The optional `tests/renderer/geometry_render.py` uses lawful external assets and
Quake3e OpenGL with SDL offscreen, engine screenshots and disabled input devices.
It is not part of ordinary CTest; see [reproduction and evidence](GEOMETRY-OPTIMIZATION.md).

## Compiler order validation

`meta_order` compiles a model grid and a material/patch room with unused shader
padding, reversed definitions and a later conflicting duplicate at one/four
workers. It requires identical BSP lump payloads and PRT bytes, plus final baked
payload parity for eight full pipelines. Twelve additional Jedi Academy builds
exercise native and converted patches and all four patch lightmap channels.
The fourth channel of ordinary brush vertices may contain the existing temporary
T-junction edge IDs; whole-lump comparisons still cover those bytes.

```sh
ctest --test-dir build/release -R '^meta_order$' -V
python tests/meta_order.py --compiler build/release/bin/q3mapx --reference /path/to/preceding/q3mapx --work-dir build/release/tests/meta-order
```

Use the appropriate executable suffix and matching Linux Release/sanitizer build
directory. Optional reference comparison retains old BSPs and checks exact
surface contents, model ownership, leaf associations and portal files after
normalizing only global surface allocation and unassigned pre-LIGHT lightmap UVs.
It records the preceding parser's uninitialized-coordinate counts separately.
A changed-vertex negative control verifies that geometry remains significant.
`validation.json` and command logs stay in the specified test directory.

## Numeric CLI validation

`cli_numbers` exercises integer, double and float options through the real
compiler. Nine call paths cover global settings, BSP, LIGHT, minimaps,
decompilation and BSP evidence. Malformed values must exit normally with an
option-specific diagnostic while preserving source and previous outputs.
Equivalent valid spellings compile actual geometry, bake lighting, generate a
CPU minimap and recover a MAP; all stored BSP lumps or complete output bytes
must agree, except the entity `_q3map2_cmdline` provenance value when argument
spellings differ. The reference comparison uses identical arguments and requires
all stored BSP lumps, including that value, to match. Signed 32-bit endpoints are
exercised with native BSP controls.

```sh
ctest --test-dir build/release -R '^(cli_numbers|cli_options|compiler_pipeline|minimap|decompile_recovery|bsp_evidence)$' --output-on-failure -j 2
python tests/cli_numbers.py --compiler build/release/bin/q3mapx --reference /path/to/preceding/q3mapx --work-dir build/release/tests/cli-numbers
```

Use `.exe` on Windows and the matching Linux/sanitizer build directory.
`--reference` runs only valid cases through the preceding binary, comparing
equivalent numeric spellings with current results. The normal CTest needs no
reference binary, assets or GUI and has a 300-second timeout. Per-command logs
and `results.json` remain in the test directory. Keep the existing `cli_options`
VIS/profile checks alongside this matrix. See [accepted syntax](CLI-INPUT.md)
and [validation evidence](validation/cli-numbers.json).

## MAP patch input validation

`patch_input` covers dimensions before allocation, strict patch numbers, matrix
shape/truncation, shared-tokenizer EOF limits and preservation of previous outputs.
It also exercises valid native/converted patches and successful entity-only
updates. Run it with the matching Windows/Linux/sanitizer build:

```sh
ctest --test-dir build/release -R '^patch_input$' -V
python tests/patch_input.py --compiler build/release/bin/q3mapx --reference /path/to/preceding/q3mapx --work-dir build/release/tests/patch-input
```

Use `.exe` on Windows. Optional reference comparison requires identical stored
BSP lumps for every valid case; it never runs huge allocation requests through
the preceding unsafe parser. Failed inputs must exit normally with diagnostics
and preserve existing files. Reports and individual command logs remain in the
test directory. See [accepted input and limits](MAP-INPUT.md).

## Script and entity input validation

`script_input` exercises the real CLI with Quake, brush-primitive and Valve 220
MAPs in IBSP/RBSP profiles; loose/packed includes, fragments and shader includes;
and literal entity values through native BSP decompilation. Invalid cases cover
entity/brush truncation, missing values, include limits/cycles, and BSP entity
directives. Failures must preserve source and previous outputs and have neither
sanitizer diagnostics nor allocation-failure fallbacks.

```sh
ctest --test-dir build/release -R '^script_input$' -V
python tests/script_input.py --compiler build/release/bin/q3mapx --reference /path/to/preceding/q3mapx --work-dir build/release/tests/script-input
```

Use `.exe` on Windows and the matching build directory for Linux/sanitizers.
The optional reference runs only the valid syntax that the preceding parser
supported, with exact stored-lump comparison. It never receives cyclic/oversized
input. Native reports and command logs remain in `tests/script-input/` beneath
the selected build; this group has its own generated game VFS and needs no assets.
See [input behavior and limits](MAP-INPUT.md).

## MAP brush input validation

`brush_input` exercises finite whole-token numbers, 32-bit legacy flags,
degenerate planes, large-distance hashing and derived texture overflow in all
three supported brush syntaxes. Valid cases cover ordinary/meta IBSP and RBSP
builds; rejected cases must retain the source and previous outputs, including
entity-only, LIGHT, conversion, region and editor-temporary paths.

```sh
ctest --test-dir build/release -R '^brush_input$' -V
python tests/brush_input.py --compiler build/release/bin/q3mapx --reference /path/to/preceding/q3mapx --work-dir build/release/tests/brush-input
```

Use `.exe` on Windows and the matching build directory for Linux/sanitizers.
The optional reference compares all stored BSP lumps on supported valid input.
Degenerate-side fixtures compare against clean controls in the new compiler;
the preceding compiler is not given the invalid-index reproduction. Large-plane
controls test only entity updates, without constructing giant-world BSP geometry.
The group has a 600-second CTest timeout. Reports and per-command logs remain in
`tests/brush-input/` under the selected build. See [accepted input](MAP-INPUT.md).

## Multi-triangle UV recovery

The independent numeric oracle and native recovery/rebuild matrix run through
CTest. The native matrix can also compare the explicit compatibility policy
against a preceding executable:

```sh
ctest --test-dir build/release -R '^uv_(fit|fit_oracle|recovery|output)$' -V
python tests/uv_recovery.py --compiler build/release/bin/q3mapx --reference /path/to/preceding/q3mapx --work-dir build/release/tests/uv-recovery
python tests/uv_output.py --compiler build/release/bin/q3mapx --reference /path/to/9bb349c/q3mapx --work-dir build/release/tests/uv-output
python benchmarks/uv_recovery.py --compiler build/release/bin/q3mapx --reference /path/to/preceding/q3mapx --work-dir build/release/tests/uv-recovery-benchmark
```

Use `.exe` on Windows and the corresponding Linux/sanitizer build directory.
`uv_recovery` has a 600-second timeout for instrumented native subprocesses.
Reports/logs stay beneath the requested directory. The benchmark alternates five
measured runs after warmup and separately verifies deterministic output, exact
legacy MAP compatibility and unchanged non-UV MAP content. It measures added
recovery cost, not a speedup. Optional `--map`, `--game` and `--game-root` use an
independent native BSP copy and read-only assets without redistributing either.
See [the feature's scope and limits](UV-RECOVERY.md).

`uv_output` has a 300-second timeout and verifies absolute UVs, native patch
controls and output-representation fallbacks. Its optional reference must support
the triangle policy (9bb349c or newer); it checks previous default losses and
explicit compatibility separately. `uv_recovery` and its benchmark discover
whether the reference supports that policy and select it when available.
The seam/candidate-limit controls compare rebuilt texture fields, accounting for
one uniform integer bias in the compatibility path, instead of requiring the
two serializers to spell the same transform identically. Absolute-output tests
allow no such bias. Benchmark patch positions are compared as parsed binary32
values, while their UVs are treated as texture definitions.

## Baked lighting observations

`lighting_evidence` exercises the optional `-bsp-evidence -lighting` report.
Exact-rational half-plane oracles check rotated, mirrored and sheared atlas
coverage/edge multiplicity independently of the C++ barycentric implementation.
Analytic affine fields check geometric positions. Fixtures retain exact encoded
RGB/RGBA/styles, 128/512 page dimensions, separate surface/model associations and
Raven lightgrid dictionary expansion. One/four-worker reports must match exactly.

```sh
ctest --test-dir build/release -R '^lighting_evidence$' -V
python tests/lighting_evidence.py --compiler build/release/bin/q3mapx --work-dir build/release/tests/lighting-evidence
```

Use `.exe` on Windows and the corresponding build directories on Linux and
ASan/UBSan. The matrix includes constant/degenerate UVs, invalid page references
and lump sizes, patch controls, ambiguous geometry/normals, extreme UVs, zero
normals, ownership overlaps, invalid grid pitch/counts and unsupported readers.
Budget boundaries, invalid options, non-finite active UVs with/without `-force`
and a 64 MiB serialization failure must retain source and previous output.

Five generated native BSP/VIS/LIGHT bakes cover a point light, sun-only,
emitter-only, mixed deluxe/bounce and Raven point lighting. Removing retained
light entities must not change lighting observations. They validate evidence
extraction, not inverse-light localization. Fixtures/logs and `results.json`
remain beneath the selected build's `tests/lighting-evidence/`. No installed
assets, editor, engine or input control is needed. See
[the supported interpretation and limits](LIGHTING-EVIDENCE.md).

`bezier_uv` tests the standalone bounded inverse against 512 independent
polynomial queries. Exact rational power-to-Bernstein conversion generates affine,
rotated/reflected/sheared, folded and coupled biquadratic fields. Known roots must
lie within the reported parameter radii; regular queries must include every
expected root, while singular queries must remain unresolved. A rotated rank-one
chart reaches the 1,023-node ceiling. Outside and parameter-boundary cases test
exclusion and the distinction between an interior enclosure and boundary tolerance.

`lighting_curves` checks 139 native curved/constant reports across IBSP, Raven
RBSP and Qfusion FBSP, plus two worker-limit failures that preserve the BSP and
previous report. Independent analytic inverses and 70-digit decimal polynomial
solves validate texel coverage, XYZ and derivative normals. Stored normal fields
have a separate interpolation oracle. Cases include folds, genuinely coupled
UVs, bowed chart boundaries, multiple tiles, styles, zero geometry/normals and
large translations. Exact-center/off-center/outside constant UVs check correlated
texel support and weights. One/four-worker reports agree; stride and combined
observation accounting include the new arrays. The earlier `lighting_evidence`
matrix additionally checks indexed-triangle constant regions and actual bakes.

```sh
cmake --build build/release --target q3mapx bezier_uv_test
ctest --test-dir build/release -R '^(bezier_uv|lighting_curves|lighting_evidence)$' -V
python tests/bezier_uv.py --unit build/release/bin/bezier_uv_test --work-dir build/release/tests/bezier-uv
python tests/lighting_curves.py --compiler build/release/bin/q3mapx --work-dir build/release/tests/lighting-curves
```

The core and native tests have 180- and 600-second CTest timeouts. Their respective
build-local `tests/bezier-uv/` and `tests/lighting-curves/` directories retain inputs,
outputs, logs and `results.json`. Qualification is recorded in
[curve/constant-region evidence](validation/lighting-curves.json). These tests
validate stored tensor geometry associations, not the original bake rays,
runtime LOD, decoded irradiance or source-light localization.

## Direct lighting probes

`light_probes` drives the native CLI using generated IBSP, Raven RBSP and Qfusion
FBSP scenes. Independent point/spot equations check colored, negative, linear,
styled and half-Lambert responses, with a 2e-4 absolute RGB-component tolerance.
One/four-worker reports must agree exactly. Supplied known light parameters restore
the direct field after stripping a retained light; this verifies forward
equivalence and does not test inverse localization.

```sh
ctest --test-dir build/release -R '^light_probes$' -V
python tests/light_probes.py --compiler build/release/bin/q3mapx --work-dir build/release/tests/light-probes
python tests/lighting.py --compiler build/release/bin/q3mapx --reference /path/to/preceding/q3mapx --work-dir build/release/tests/light-probes-reference
```

Use `.exe` on Windows and the matching build directory on Linux/ASan/UBSan.
The probe matrix has a 600-second timeout. It covers sun/emitter provenance,
backsplash suppression, proposed suns, alpha/filter image mutations and the
`-notrace` control. Poisoned original MAP/SRF files and a generated-shader sentinel
check that the diagnostic uses BSP data and preserves existing files. Invalid
JSON/numbers, unsupported flags, input/report aliases, source/sample limits and
worker response overflow must retain previous outputs. Native integer spellings
must agree with compiler parsing; decimal/exponent forms in integer keys fail.
Fixtures, requests, reports, logs and `results.json` remain under the selected
build's `tests/light-probes/`.

The separate material suite compares ordinary bake vertices, surfaces, lightmaps
and grids against the preceding executable, across adaptive, bounce, deluxe,
supersampled, random, dirt and flood modes. It also checks dense-grid and varied
worker counts. These comparisons guard the shared lighting setup changes; direct
probes deliberately omit complete bake effects. See [the contract](LIGHT-PROBES.md)
and [recorded evidence](validation/light-probes.json). Trace-node exhaustion,
every scene/source/serialization ceiling and complete missing-asset provenance
still need dedicated qualification. No editor or renderer is launched by these
tests.

## Baked-lightmap hypothesis comparison

`light_comparison` exercises automatic `baked_lightmaps` requests through the
native CLI. Closed-room bakes in IBSP, Raven RBSP and Qfusion FBSP cover linear,
gamma/compensated, sRGB, exposure/contrast/saturation/brightness and clipped-light
settings. Selected interior texels must match the actual bake exactly. Raven and
Qfusion add styled spotlights to verify slot/style separation and that secondary
slots do not acquire global ambient/minlight. One/four-worker reports agree.

```sh
ctest --test-dir build/release -R '^light_comparison$' -V
python tests/light_comparison.py --compiler build/release/bin/q3mapx --work-dir build/release/tests/light-comparison
```

Use `.exe` on Windows and the corresponding Linux/sanitizer directories. The
group has a 600-second timeout. Independent scalar transfer arithmetic checks
the encoded floats within 2e-3 component units; native lump reads independently
check every observed RGB, and recomputed errors check summary accounting. Wrong
encoding and misplaced proposals must score worse. Restoring supplied known
parameters after stripping a light must reproduce the baseline score; this is
not a candidate-generation or localization test.

Additional bakes exercise stored curves, surviving inline-model origins,
sun-only/emitter-only/mixed material lighting and an unexplained bounce component.
These cases retain measured residuals and unknown traces rather than asserting
full-bake equivalence. Missing/constant/zero-normal observations must produce null
error metrics; an overflowing encoding must remain unknown without unsafe byte
conversion. Malformed selection, work/observation/sample limits and nonfinite
active UVs preserve source and previous report files. Exact work/observation/sample
ceilings pass, while one unit less fails; selecting one surface retains the same
associations and whole-scene extraction costs. Constant-region exclusions count
all primitives even when the representative records are strided. Poisoned MAP/SRF files and
a generated-shader sentinel check read-only behavior.

Inputs, requests, reports, logs and `results.json` remain in the build's
`tests/light-comparison/`. See [the request/report contract](LIGHT-PROBES.md#automatic-internal-lightmap-comparison)
and [validation evidence](validation/light-comparison.json). The shared encoding
extraction also requires normal-bake reference parity through `tests/lighting.py`;
synthetic arithmetic checks alone do not establish unchanged compiler output.

## Point-light fitting

`point_fitting` exercises blind `fit_point_lights` requests through the native CLI.
Generated source lights are stripped and the MAP/SRF are poisoned before search.
Off-grid positions, overlapping colors, locked surviving lights, opaque occlusion,
native light styles, gamma/sRGB/coupled encoding and entity sRGB are covered.
Successful trials are added to independently decompiled MAPs and run through
BSP/VIS/LIGHT; comparison uses world position/normal/style associations instead
of assuming atlas coordinates survived the rebuild.

```sh
ctest --test-dir build/release -R '^point_fitting$' -V
python tests/point_fitting.py --compiler build/release/bin/q3mapx --work-dir build/release/tests/point-fitting
```

Use `.exe` on Windows and corresponding Linux/sanitizer build directories. The
group timeout is 900 seconds. One/four-worker report equality, exact/one-below
work budgets, blocked search regions, missing/saturated observations and material
prerequisites are checked. Changing only withheld atlas blocks must preserve
the search result and training score while rejecting validation. Sun-only and
emitter-only fixtures must require no added point lights; wrong gamma and the
bounced control must fail the score gate. Trials/limits remain conditional on
current assets and declared encoding, not general recovery accuracy guarantees.

For a short material regression check, pass `--material-only` and use a separate
`--work-dir build/release/tests/point-materials`. This deliberately keeps a valid
fallback texture while requesting an unavailable `q3map_lightimage`; both values
of `allow_implicit_materials` must reject the fit. Use the full matrix to qualify
the search and recovered-map rebuilds.

The shared single-light envelope extraction requires ordinary bake parity with
the preceding compiler through `tests/lighting.py`. The related probe/comparison
matrices also run with corrected Qfusion material paths and asserted effective
sRGB flags; earlier sRGB-labelled comparison cases were overridden to linear.
Native inputs, reports and logs remain in each build's `tests/point-fitting/`;
[validation evidence](validation/point-fitting.json) records these checks and
their limits. No editor, game renderer or user-input control is used.

## Spotlight fitting

`spot_fitting` exercises native `fit_spot_lights` requests with original lights
and optional targets stripped, original MAP/SRF poisoned, and hidden source
labels used only for accuracy evaluation. Known-source cases compare position,
direction and cone errors separately from actual recovered-map BSP/VIS/LIGHT
rebuilds. Native encoding/styles, overlapping sources, fixed retained lights,
static/duplicate/unsafe targets and generated-name collisions are covered.

```sh
ctest --test-dir build/release -R '^spot_fitting$' -V
python tests/spot_fitting.py --compiler build/release/bin/q3mapx --work-dir build/release/tests/spot-fitting
```

Use `.exe` on Windows. `--case <name>` selects a fixture during investigation;
only the complete matrix qualifies a build. The group timeout is 1800 seconds,
or 5400 seconds with sanitizers for the complete instrumented native searches.
One/four-worker reports, altered withheld blocks, exact/one-below work budgets
and malformed options check determinism and report preservation. Separate
illuminated-support scores prevent a dark background from diluting fit errors.
An optional `--reference <prior-compiler>` demonstrates the earlier dangling
target bug: a temporary proposed target could incorrectly change an unrelated
retained light from point to spot. Current retained responses must stay unchanged.

The shared native encoder now permits continuous sRGB output for optimizer
derivatives; ordinary baking and report qualification retain native rounding.
Re-run `light_comparison`, `point_fitting` and ordinary reference bake parity
when changing this path. See [the spotlight contract](SPOT-FITTING.md) and
[validation evidence](validation/spot-fitting.json) for assumptions, measured
errors and unsupported recovery cases. No renderer or input control is used.

## Applying light proposals to recovered MAPs

`light_recovery` fits stripped native point/spot scenes and applies their reports
through `-decompile -light-proposals`. It then runs independent BSP/VIS/LIGHT
rebuilds, matching world positions, normals and styles across changed atlas
packing. Evidence for inline brush models is translated by the rebuilt entity's
origin before comparison. Original MAP/SRF files are poisoned before fitting.

```sh
ctest --test-dir build/release -R '^light_recovery$' -V
python tests/light_recovery.py --compiler build/release/bin/q3mapx --work-dir build/release/tests/light-recovery
```

Use `.exe` on Windows and the corresponding Linux/sanitizer build directories.
The group timeout is 1800 seconds. `--case <name>` selects an investigation;
the complete matrix covers all three MAP formats, fast/full recovery, native
styles, sRGB colors, Wolf falloff, retained targets/sources, fixed point/spot and
culled dependencies, and detail/group recovery with an inline brush model.
Malformed reports, inconsistent scores/texels, unsafe target links, excessive
resources and direct/hard-link output aliases must preserve existing outputs.
An optional `--reference <prior-compiler>` checks default MAP byte parity.

Keep the shared `recovery_outputs` publication checks and related native
probe/comparison/fitting groups passing. Stored-score consistency checks do not
rerun transport or authenticate a report: actual MAP rebuild comparisons are
separate validation. See [the export contract](LIGHT-RECOVERY.md) and
[validation evidence](validation/light-recovery.json). No renderer or user-input
control is needed for these tests.

## Workbench light recovery

`workbench_lights` runs the real Qt window against native point, spot and Qfusion
sRGB fixtures. Hidden light entities are stripped and source MAP/SRF files are
poisoned. Direct widget actions submit fitting jobs, review reports, select exact
report bytes and decompile through the queue. The Python harness then runs
independent BSP/VIS/LIGHT rebuilds and compares corresponding world observations
with the source bake and the fit's predictions.

```sh
ctest --test-dir build/release -R '^(workbench_lights|game_catalog|workbench|workbench_window|workbench_preview|workbench_devices|workbench_inspector|game_profiles)$' --output-on-failure -j 2
python tests/workbench_lights.py --test build/release/bin/workbench_lights_test --compiler build/release/bin/q3mapx --work-dir build/release/tests/workbench-lights
```

Use `.exe` for both binaries on Windows. The new group has a 1800-second timeout
and needs Qt Widgets; `QT_QPA_PLATFORM=offscreen` is set by the harness. All inputs,
state, snapshots, PNGs and outputs remain in the requested test folder. The window
is painted directly into QImages, without OS capture or input injection. Tests
also cover precision-preserving settings, old compiler/menu guards, pinning before
directory creation, source/game mismatch, malformed/deep/oversized JSON, unavailable
scores, selection stability, stale replies and native process cancellation.

For address/undefined checks, enable `Q3MAPX_BUILD_GUI=ON` in a Linux sanitizer
build, build `q3mapx` and `workbench_lights_test`, then run only `workbench_lights`.
This is a local override; the standard sanitizer preset remains Qt-free. Retain
its Debug `-g -O1`, assertions and established `ASAN_OPTIONS=detect_leaks=0`
configuration. Record which targets and groups were actually tested rather than
claiming a complete sanitizer run. [Validation evidence](validation/workbench-light-recovery.json)
records final binaries, sources, logs and independent rebuild results.

## LIGHT source loading

```sh
ctest --test-dir build/release -R '^(light_source|brush_input|patch_input|script_input|surface_density|patch_paint|compiler_pipeline|lighting_materials)$' --output-on-failure -j 2
python tests/light_source.py --compiler build/release/bin/q3mapx --reference path/to/prior/q3mapx --work-dir build/release/tests/light-source
```

Use `.exe` on Windows and the corresponding Linux/sanitizer build directories.
The independent LIGHT-source group has a 600-second timeout and requires no game,
editor or installed assets. Generated fixtures include unused-first/empty shader
tables and discarded source-only materials. Every bake must match a source with
the same lights and no brushes, and removing those lights must change the bake.
Malformed inputs must retain diagnostics and preserve existing outputs. The
optional reference reads only the no-brush oracle, avoiding the former unsafe
lookup. Keep the existing parser, authoring and material-lighting groups passing.

## Compiled patch channel recovery

Compiled patch channel recovery has a separate generated corpus:

```sh
ctest --test-dir build/release -R '^(patch_color_recovery|patch_paint|decompile_recovery|recovery_outputs|uv_recovery)$' --output-on-failure -j 2
python tests/patch_color_recovery.py --compiler build/release/bin/q3mapx --reference path/to/prior/q3mapx --work-dir build/release/tests/patch-color-recovery
```

Use `.exe` on Windows. The optional reference checks default MAP/report byte
parity; opt-in recovery is checked by independent control and sampled-field
comparisons after recompilation and relighting. See [the contract and guards](PATCH-COLOR-RECOVERY.md).

## Retained patch sources

Retained source recovery is independently checked by `patch_source`:

```sh
ctest --test-dir build/release -R '^(patch_source|patch_color_recovery|patch_paint|decompile_recovery|recovery_outputs|uv_recovery|bsp_validation)$' --output-on-failure -j 2
python tests/patch_source.py --compiler build/release/bin/q3mapx --reference path/to/prior/q3mapx --work-dir build/release/tests/patch-source
```

The optional renderer harness `tests/renderer/patch_source_render.py` accepts a
Quake3e executable/source tree, read-only game assets, generated material-fixture
assets, compiler and in-project work directory. It compares engine screenshots
of identical native lump payloads with/without the archive, with SDL offscreen,
windowed mode and disabled input/network. See [archive format and limits](PATCH-SOURCE.md).

## Triangle-only patch inference

Triangle-only inference has an independent mathematical oracle and a native BSP
round-trip harness:

```sh
ctest --test-dir build/release -R '^(patch_fit|patch_reconstruction|patch_source|patch_color_recovery|decompile_recovery|recovery_outputs|patch_paint)$' --output-on-failure -j 2
```

Use `tests/renderer/patch_source_render.py --fit` with the same in-project engine
test setup to compare original and reconstructed triangle-only scenes. It changes
only independent copied test assets. See [contract and evidence](TRIANGLE-PATCH-RECOVERY.md).

Grid discovery now uses a complete connectivity check instead of affine texture
coordinates. `patch_fit` independently evaluates shuffled flat/curved grids with
affine, warped, constant and folded UV fields, including multiple spans on both
axes. `patch_reconstruction` also rebuilds and relights archive-free Q3/JA examples
of each newly supported field, comparing oriented XYZ/ST/requested-color samples.
Malformed interior diagonals, holes, unused samples and exhausted work remain
rejections. `patch_source` checks individual restored settings in the JSON against
the source primitives; `workbench_patches` covers those rows, malformed metadata
and compatibility with older aggregate reports.

## Workbench patch recovery

`workbench_patches` runs ten Quake III/Jedi Academy scenes through the real Qt
window and compiler queue: retained sources, archive-free fitting, ordinary
native colors and both automatic paths. Adjacent MAP/SRF files are poisoned.
After review, the Python harness independently rebuilds and relights each MAP,
comparing oriented triangle positions, UVs and requested compiled color bytes.

```sh
ctest --test-dir build/release -R '^(workbench_patches|workbench_lights|game_catalog|workbench|workbench_window|workbench_preview|game_profiles)$' --output-on-failure -j 2
python tests/workbench_patches.py --test build/release/bin/workbench_patches_test --compiler build/release/bin/q3mapx --work-dir build/release/tests/workbench-patches
```

Use `.exe` on Windows. The harness sets `QT_QPA_PLATFORM=offscreen`; it paints
owned widget trees into images without OS capture or input injection. Cases
check project/snapshot persistence, default compatibility, older compiler and
read-only profile guards, malformed/oversized/truncated JSON, cancellation,
supersession, a 20,000-row review and compact/full layouts in both themes.
Native source and shader assets remain independent copies in the test folder.

For sanitizers, enable the established local `Q3MAPX_BUILD_GUI=ON` override and
build the targets needed for the selected groups. Keep Debug assertions and the
existing `ASAN_OPTIONS=detect_leaks=0` configuration. The ordinary sanitizer
preset remains Qt-free. [Recorded evidence](validation/workbench-patch-recovery.json)
identifies tested binaries, groups, cases, logs and widget previews.

## Task commits

Complete a coherent task, run its relevant checks, update `docs/PROGRESS.md`, and
commit and push to `origin/main`, keeping development on `main` as requested.
Do not bundle unrelated fixes into an optimization commit. Preserve
upstream copyright headers and credit any additional incorporated external code
after checking license compatibility. Record outstanding and unrelated issues.

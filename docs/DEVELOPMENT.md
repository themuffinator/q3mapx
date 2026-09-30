# Development and validation

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

Pass `--reference /path/to/uncompressed/q3mapx` to require exact VIS bytes against
a compiler with the same merge repairs and original bitset layout. The round
retains that executable and its source identity under `.agents/tmp/continuation`.
The [reference patch](../benchmarks/references/vis-uncompressed-at-7aaa22b.patch)
recreates its source from `7aaa22b` in a separate test checkout. It intentionally
predates the PRT cleanup-order fix; comparisons keep the PRT in both executables.
`benchmarks/vis_portals.py` alternates those binaries with an optional preceding
legacy executable, records whole-process medians, requested passage storage and
per-cluster visible surface/triangle distributions. Existing merge-mode PVS
differences are recorded separately from required compaction parity; this is not
a renderer benchmark or validation of the planned intelligent transformation.

`binary_outputs` checks native IBSP/RBSP publication and shared profile saves
against real Windows sharing locks and POSIX file-size limits. Empty, ordinary
and large-lump inputs distinguish buffered header-seek and immediate write
failures. It requires original preservation, clean staging, successful retry,
directory/link rejection and no premature success message. Run it with
`--reference /path/to/previous/q3mapx` for full output-byte parity after masking
only the unused timestamp. `atomic_file` also checks 64-bit positions, invalid
operations and a real read-only stream error. See [output safety](OUTPUT-SAFETY.md)
for the covered writers and failure guarantees.

## Task commits

Complete a coherent task, run its relevant checks, update `docs/PROGRESS.md`, and
commit and push to `origin/main`, keeping development on `main` as requested.
Do not bundle unrelated fixes into an optimization commit. Preserve
upstream copyright headers and credit any additional incorporated external code
after checking license compatibility. Record outstanding and unrelated issues.

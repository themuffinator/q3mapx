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

## Task commits

Complete a coherent task, run its relevant checks, update `docs/PROGRESS.md`, and
commit. Do not bundle unrelated fixes into an optimization commit. Preserve
upstream copyright headers and credit any additional incorporated external code
after checking license compatibility. Record outstanding and unrelated issues.

# Development and validation

## Build policy

Requires CMake 3.25+, Ninja, a C++20 compiler, pkg-config, GLib, libxml2, Assimp,
libpng, libjpeg, and zlib. Qt 6 will be required by the GUI milestone. No game
assets are needed to build the compiler. GCC/MinGW is the initially validated
toolchain; MSVC is not yet validated against all inherited source constructs.

On Windows, install the following from an MSYS2 MINGW64 shell if not already present:

```sh
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja mingw-w64-x86_64-pkgconf mingw-w64-x86_64-glib2 \
  mingw-w64-x86_64-libxml2 mingw-w64-x86_64-assimp mingw-w64-x86_64-libpng \
  mingw-w64-x86_64-libjpeg-turbo mingw-w64-x86_64-zlib
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
libxml2-dev libassimp-dev libpng-dev libjpeg-dev zlib1g-dev` and use the same CMake
commands. The executable is `build/release/bin/q3mapx`.

`debug` and `profile` presets provide debug and optimized-with-symbols builds.
Optional `-DQ3MAPX_ENABLE_LTO=ON` enables release IPO after a compiler capability
check; `-DQ3MAPX_ENABLE_SANITIZERS=ON` enables ASan/UBSan on supporting GCC/Clang
toolchains. Baseline comparisons use LTO off and no fast-math.

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

## Task commits

Complete a coherent task, run its relevant checks, update `docs/PROGRESS.md`, and
commit. Do not bundle unrelated fixes into an optimization commit. Preserve
upstream copyright headers and credit any additional incorporated external code
after checking license compatibility. Record outstanding and unrelated issues.

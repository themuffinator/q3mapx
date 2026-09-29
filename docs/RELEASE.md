# Installation and release packaging

## Portable Windows build

Extract the complete `q3mapx-0.3.0-windows-x64.zip` folder, then run
`bin/q3mapx-workbench.exe` or `bin/q3mapx.exe -help`. Keep the DLLs, Qt plugins,
`qt.conf`, documentation and license notices with the executables. The package
does not require MSYS2 on PATH and does not install services or change the system.
User GPU drivers supply optional OpenCL support; no GPU SDK is needed. CPU
compilation and minimaps remain available without a compatible GPU runtime.

The workbench discovers the sibling compiler. Select a game root containing the
appropriate game assets, a MAP/BSP source and an output directory. See the
[workbench guide](WORKBENCH.md), [decompilation guide](DECOMPILATION.md), and
[performance/options reference](PERFORMANCE.md). No game assets are included.

The current release is a development release. Windows x64/MinGW and Linux x64
(Ubuntu 24.04 under WSL, GCC 13 and Qt 6.4) are locally validated, including Linux
ASan/UBSan checks. MSVC, macOS, manual accessibility testing and compatibility
inside the games themselves remain unverified. Version 0.3.0
adds indexed Raven lightgrid serialization, a compiler-driven 25-profile GUI
catalog, bounded native inspection, Alice/F.A.K.K.2/MOHAA/early-Quake-III recovery,
FTX textures, terrain/placement metadata, and parallel curved OBJ/ASE exports.
Nineteen profiles retain native writers; the six new profiles are recovery-only.
The [coverage ledger](GAME-COVERAGE.md) separates generated pipelines, native-map
parsing, recovery limits and gameplay claims.

The package's `runtime-manifest.json` identifies its exact clean source revision,
runtime files and dependency sources. Its adjacent SHA-256 file identifies the
ZIP. Final integration and portable validation are recorded in the
[task log](PROGRESS.md); the previous [0.2.0 audit](releases/0.2.0-windows-x64.json)
remains available as historical evidence. Archives stay under `build/package/`
and have not been published remotely.

## Recreate the Windows package

Build and test the release preset, commit its source, then run from the project:

```powershell
python tools/package_windows.py --msys-root C:/msys64 --fetch-dependency-sources
python tests/package_smoke.py --package-dir build/package/q3mapx-0.3.0-windows-x64 --work-dir build/package-validation-0.3.0 --workbench-test build/release/bin/workbench_test.exe
```

The packager requires Python 3.9+, the configured MinGW toolchain, CMake,
`objdump`, `qtpaths6` and `windeployqt6`. `--build-dir` selects a different build;
`--cli-only` omits the workbench. `--prefix ucrt64` supports that MSYS2 layout but
is not part of the initial locally tested matrix. Output always stays under
`build/package/`. Existing package directories are rejected rather than overwritten.
Use `--name` for another artifact name. `--allow-dirty` explicitly labels a local
development snapshot and includes its current source; release packages should use
a clean committed source tree. Before creating the output directory, the script
checks that the CLI and workbench report the source tree's release version.

Packaging retrieves the MSYS2 source archive index to select the exact installed
version and compression format. For offline use, pass `--source-index` with a
previously saved index; downloading dependency archives still requires network
access unless those exact archives are already in `dependency-sources/`.

The script installs the project, deploys unmodified Qt libraries and selected
plugins, recursively resolves PE imports, and fails for missing dependencies or
license notices. Windows system libraries and OpenCL drivers are not copied.
It writes:

- A portable directory and ZIP, plus SHA-256 checksum.
- `runtime-manifest.json` with executable/DLL hashes, imports, package ownership,
  exact installed versions, license metadata and corresponding source locations.
- `licenses/runtime/` and `RUNTIME-CREDITS.md` with third-party notices and credits.
- `q3mapx-source.zip` containing the exact project source used for the package.
- With `--fetch-dependency-sources`, exact versioned MSYS2 source packages in the
  adjacent `dependency-sources/` folder, including source and build recipes.

Keep the dependency source archives available alongside any distributed binary
archive. The manifest records their hashes when downloaded. Runtime libraries
remain dynamically linked and replaceable with ABI-compatible builds. Qt is not
patched; `qt.conf` makes plugin lookup relative to the application. This software
is based in part on the work of the Independent JPEG Group.

Archive member ordering and timestamps are fixed using the source commit time
(or `SOURCE_DATE_EPOCH`). Rebuilding with different toolchain binaries, source
line endings, installed dependency versions or compiler timestamps can still
change executable/archive hashes; this is a reproducible packaging procedure,
not a claim of bit-identical cross-toolchain builds.

The smoke harness verifies recorded file hashes and runs full compile/recovery,
CPU/GPU minimap and lighting checks, the Qt queue integration and direct offscreen widget
rendering with development dependency directories removed from PATH. It retains
logs, a GUI preview and `validation.json` in the selected project-local work folder.
It also covers all native writers, the new recovery families, curved mesh exports,
Raven compatibility/packing and bounded native inspection using generated inputs.

## Known limits

- Automatic GPU acceleration applies to sufficiently large minimaps. Lighting
  defaults to CPU; its opt-in GPU polygon-factor backend is experimental and has
  not reduced measured complete-bake time. See [GPU-LIGHTING.md](GPU-LIGHTING.md).
- Default legacy VIS scheduling can vary a few visibility bits between runs on
  some inputs. Select `-vis -reproducible` for repeatability across worker counts.
- Decompilation recovers compiled geometry and texture data; it cannot recreate
  deleted editor metadata or uniquely infer original modeling intent.
- Compiler-owned BSP/PRT input and numeric boundaries are checked. Third-party
  image/model decoders still need broader fuzz/sanitizer coverage.
- Legacy fatal exits may omit CPU profiles and leave an identifiable sibling
  temporary file. Completed BSP writes use atomic replacement.
- Imported `UnsortedSet` code still emits non-standard-layout `offsetof` warnings.

See [the task log](PROGRESS.md) for the exact validation performed and remaining
issues. Unsigned development binaries have no installer or code-signing certificate.

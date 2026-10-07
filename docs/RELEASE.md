# Installation and release packaging

## Portable Windows build

Extract the complete `q3mapx-0.4.0-windows-x64.zip` folder, then run
`bin/q3mapx-workbench.exe` or `bin/q3mapx.exe -help`. Keep the DLLs, Qt plugins,
`qt.conf`, documentation and license notices with the executables. The package
does not require MSYS2 on PATH and does not install services or change the system.
User GPU drivers supply optional OpenCL support; no GPU SDK is needed. CPU
compilation and minimaps remain available without a compatible GPU runtime.

The workbench discovers the sibling compiler. Select a game root containing the
appropriate game assets, a MAP/BSP source and an output directory. See the
[workbench guide](WORKBENCH.md), [decompilation guide](DECOMPILATION.md), and
[performance/options reference](PERFORMANCE.md). No game assets are included.

## Linux build

Download `q3mapx-0.4.0-ubuntu-24.04-x64.tar.gz` from
[GitHub Releases](https://github.com/themuffinator/q3mapx/releases/latest).
This archive is built on Ubuntu 24.04 x64 and uses system libraries. On that
release of Ubuntu, install the runtime dependencies, extract the archive and
run `bin/q3mapx-workbench` or `bin/q3mapx -help`:

```sh
sudo apt-get install libglib2.0-0t64 libxml2 libassimp5 libpng16-16t64 \
  libjpeg-turbo8 zlib1g libqt6widgets6 libqt6gui6 libqt6core6t64
```

Other Linux distributions can build from source using the existing CMake presets.
The Linux archive does not bundle or replace system libraries. Both platform
archives include documentation, project licences and a runtime manifest.

## Versions and changelog

[`VERSION`](../VERSION) is the single version source for CMake, the CLI,
workbench and packaging. Use numeric `MAJOR.MINOR.PATCH` versions with matching
`vMAJOR.MINOR.PATCH` Git tags. While below 1.0, minor releases may change
experimental interfaces; patch releases are compatible fixes.

Record user-visible changes under `Unreleased` in [`CHANGELOG.md`](../CHANGELOG.md),
using Added, Changed, Fixed, Removed or Security headings as needed. Keep notes
useful to users; the detailed task log remains in `PROGRESS.md`. Before a release:

```sh
python tools/release.py prepare 0.4.1
python tools/release.py check --version 0.4.1
```

`prepare` dates the Unreleased notes, clears that section, updates comparison
links and changes VERSION. It refuses empty notes, reused versions and downgrades.
Review and commit both files together. CMake rebuilds both version banners when
VERSION changes. Ordinary CI validates the metadata and the release tools.
Versions 0.1.0 through 0.3.0 identify historical local packages; 0.4.0 is the
first public GitHub release.

## Publishing a release

1. Prepare the version and changelog, then push the reviewed commit to `main`.
2. Open [Actions → Release](https://github.com/themuffinator/q3mapx/actions/workflows/release.yml)
   and select **Run workflow** on `main`. Enter the exact version from VERSION.
3. Leave **Publish** unchecked for a rehearsal, or check it to publish after
   validation. **Prerelease** optionally marks the release as a preview and
   keeps it from replacing GitHub's Latest release.
4. Follow the run to completion. A rehearsal exposes the complete `release-ready`
   artifact; a published run also links to the public release in its summary.

The equivalent publish command is:

```sh
gh workflow run release.yml --ref main -f version=0.4.0 -F publish=true
```

The workflow pins one source commit, runs the complete `release` CTest suite on
Windows and Ubuntu, packages both builds and exercises their packaged CLI/Qt
runtimes. Windows smoke tests remove development DLL paths. Linux smoke tests
use the extracted archive. Offscreen previews come from Qt itself; no desktop
capture or input automation is used. No installed game assets are required.
Hardware-specific OpenCL checks can skip when hosted runners have no GPU.

Only the final publish job can write repository contents. After both platforms
pass, it verifies the exact asset set, creates the matching tag and a draft,
uploads every asset, checks GitHub's SHA-256 digests and sizes, then publishes.
This draft-first sequence also supports repositories with immutable releases.
No personal access token or extra secret is needed; the job uses GITHUB_TOKEN.
Actions must be enabled and repository rules must permit that token to create
the release tag. Builds use pinned action revisions, maintained by Dependabot.

Each release contains:

- Windows x64 portable ZIP and Ubuntu 24.04 x64 tar.gz.
- The exact project source ZIP, also embedded in the Windows package.
- A Windows dependency-source ZIP containing every redistributed runtime's exact
  MSYS2 source package, licences/credits index and runtime manifest.
- `release-manifest.json` binding all four archives to the version, source commit,
  file sizes and SHA-256 hashes, plus `SHA256SUMS` covering the archives and manifest.

Download all assets you need into one folder. Check them with `sha256sum -c
SHA256SUMS` on Linux, or compare `Get-FileHash -Algorithm SHA256 <archive>` with
SHA256SUMS in PowerShell. Retain the matching dependency-source ZIP when
redistributing Windows binaries. GitHub's generated source archives are also
available from the immutable version tag.

A build/test/package failure cannot publish a release. Fix the cause and run the
workflow again. If publication fails after creating a tag or draft, **Re-run
failed jobs** reuses the already-tested artifacts and can replace assets only
in the draft for that same commit. Published releases and tags pointing to a
different commit are never overwritten. A fix after publication needs a new
version. Release workflows are serialized and do not cancel an active publish.
Actions artifacts are retained for 7–14 days; published release assets persist.
A manual run can build a newer toolchain than a prior rehearsal, so publication
always repeats the checks instead of treating an earlier rehearsal as approval.

## Historical local packages

The [0.3.0 artifact audit](releases/0.3.0-windows-x64.json) and
[validation matrix](validation/release-0.3.0.json) describe the older local
archive at source revision `95c37a655e6f9d9f8c4b056464f7ed1ab1286410`.
The [0.2.0 audit](releases/0.2.0-windows-x64.json) remains historical evidence.
Those archives stay under `build/package/` and are not the current GitHub release.

## Recreate the Windows package

Build and test the release preset, commit its source, then run from the project:

```powershell
python tools/package_windows.py --msys-root C:/msys64 --fetch-dependency-sources
python tests/package_smoke.py --package-dir build/package/q3mapx-0.4.0-windows-x64 --work-dir build/package-validation-release --workbench-test build/release/bin/workbench_test.exe
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
Raven compatibility/packing, bounded native inspection and concurrent fatal exits
using generated inputs.

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

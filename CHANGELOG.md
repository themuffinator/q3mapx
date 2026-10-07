# Changelog

User-visible changes are recorded here before each release. Versions follow
[Semantic Versioning](https://semver.org/); entries use
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) categories.
While the project is below 1.0, minor releases may change experimental interfaces.

## [Unreleased]

## [0.4.0] - 2026-10-07

First public GitHub release. Includes the compiler and native Qt workbench;
0.1.0–0.3.0 were local development packages, not GitHub releases.

### Added

- Portable Windows x64 compiler/workbench package, Ubuntu 24.04 x64 archive,
  matching project source, Windows dependency sources, runtime licences,
  revision manifests and SHA-256 checksums.
- Manual GitHub Actions release workflow with a build-only rehearsal mode,
  version/changelog validation, full Windows/Linux release regressions,
  portable package checks and draft-first publication.
- Persistent CPU workers, named pass profiles, repeatable VIS, indexed minimaps
  and optional OpenCL minimap acceleration with CPU fallback.
- Native Qt workbench with saved projects, asynchronous compile queues,
  searchable logs, game/device discovery and BSP inspection.
- Twenty-five game profiles, including native Alice, F.A.K.K.2, Allied Assault
  and early Quake III recovery; nineteen profiles support native writing.
- BSP decompilation with UV fitting, brush/entity preservation, Valve 220 MAP
  output, recovery reports, curved OBJ/ASE export and bounded patch recovery.
- Geometry, portal, cell and baked-lighting evidence; direct lighting probes,
  bounded point/spotlight fitting and workbench recovery review.
- Experimental surface lightmap density, retained patch RGBA and triangle-to-patch
  recovery, a companion NRC authoring patch, and guarded post-LIGHT geometry reduction.

### Fixed

- Validate malformed BSP/PRT/MAP input and numeric options; stage output before
  publication and preserve prior outputs on reported write/close failures.
- Reject Windows symbolic-link and reparse-point output destinations even when
  the MinGW standard library reports them as ordinary files.
- Balance BSP block trees, bound longest-path depth, preserve resolved surface
  lightmap spacing and repair VIS merge/occlusion handling.

### Known limitations

- Unsigned development binaries. Windows x64 and Ubuntu 24.04 x64 are the binary
  targets; macOS, MSVC and in-game rendering compatibility are not certified.
- GPU lighting, patch/material authoring, inferred recovery and geometry reduction
  remain experimental. Decompilation cannot reconstruct deleted editor intent.
- The Linux archive uses system libraries; install the runtime dependencies in
  `docs/RELEASE.md`. OpenCL hardware tests may skip on hosted runners.

[Unreleased]: https://github.com/themuffinator/q3mapx/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/themuffinator/q3mapx/releases/tag/v0.4.0

# q3mapx

[![Build and regressions](https://github.com/themuffinator/q3mapx/actions/workflows/build.yml/badge.svg)](https://github.com/themuffinator/q3mapx/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/themuffinator/q3mapx)](https://github.com/themuffinator/q3mapx/releases/latest)

An independent, performance-focused continuation of the q3map2 compiler from
[NetRadiant-custom (NRC)](https://github.com/Garux/netradiant-custom), with a native
desktop workbench and the existing command-line workflow.

q3mapx is in active development, with persistent CPU jobs, optional OpenCL
minimap acceleration, stronger input validation, improved BSP recovery, and a
native Qt workbench. It starts from NRC revision `8216133` (latest at retrieval on
2026-09-29). See the [implementation plan](docs/PLAN.md) and [task log](docs/PROGRESS.md)
for completed work, validation and remaining limits. Download tested binaries from
[GitHub Releases](https://github.com/themuffinator/q3mapx/releases/latest) and see
the [changelog](CHANGELOG.md) for versioned release notes.

The standalone CLI builds with CMake/Ninja; see [build instructions](docs/DEVELOPMENT.md).
Release output is `build/release/bin/q3mapx` (`.exe` on Windows).
The native GUI is `build/release/bin/q3mapx-workbench`; see the [workbench guide](docs/WORKBENCH.md).
For Windows and Linux archives, dependencies, source and installation steps,
see [release packaging](docs/RELEASE.md).

The compiler provides checked, staged MAP/report and mesh output with
companion rollback on publication errors, plus an asynchronous [BSP inspection page](docs/BSP-INSPECTION.md)
in the workbench. See [export guarantees and limits](docs/DECOMPILATION.md).
BSP and shared buffer saves now also close and discard unfinished staging files
on reported write failures; see [output safety](docs/OUTPUT-SAFETY.md).
The new [`-bsp-evidence` command](docs/BSP-EVIDENCE.md) reports validated brush,
partition, regional and stored-visibility observations for inference development.
Supply a matching PRT with `-portals` for [regional portalling diagnostics](docs/PORTAL-ANALYSIS.md)
and stored-PVS world triangle costs.
Use `-cell-adjacency` to [reconstruct bounded BSP leaf-path cells and interfaces](docs/CELL-ADJACENCY.md)
without the original PRT, with explicit enclosure and numerical limits.
Use `-lighting` for [bounded baked-lighting observations](docs/LIGHTING-EVIDENCE.md)
with stored colors/styles, curved-patch inverses, constant-UV primitive footprints
and explicit mapping uncertainty.
Use [`-light -probes`](docs/LIGHT-PROBES.md) to evaluate explicit points and
proposed lights with the compiler's direct CPU model, separating retained lights,
surface emitters and sun/sky without writing source or bake outputs. Its optional
automatic texel comparison scores the encoded hypothesis against internal
lightmaps with explicit sampling assumptions and unknown/excluded observations.
Optional [point-light fitting](docs/POINT-FITTING.md) searches BSP space for
conditional missing-light proposals, with native tracing, bounded parallel work,
material checks and withheld-texel validation. Optional [spotlight fitting](docs/SPOT-FITTING.md)
also fits directions/cones and proposes retained or new target links.
[Apply qualified reports during decompilation](docs/LIGHT-RECOVERY.md) to produce
editable light entities and their target links. The [workbench light recovery page](docs/WORKBENCH.md#light-recovery)
provides native fitting controls, scored proposal review and explicit report selection
for MAP export. Unknown bake calibration, spatial proposal overlays and broader
recovery accuracy remain open.
Optional [detail and group inference](docs/DECOMPILATION.md#group-inference-policy)
can now export cell-supported detail flags and surface-supported `func_group`
assemblies, with bounded work, rebuild-order protection and explicit uncertainty.
The [geometry optimizer](docs/GEOMETRY-OPTIMIZATION.md) now provides bounded
post-LIGHT triangle reduction and reports in CLI/workbench, initially for an
explicit Quake3e OpenGL contract on eligible Quake III surfaces.
The older local 0.3.0 archive remains historical evidence; current downloads are
published through the [manual release workflow](docs/RELEASE.md#publishing-a-release).

Recover an editable map with a texture-recovery report:

```sh
q3mapx -decompile -game quake3 -fs_basepath /path/to/game -o recovered.map example.bsp
```

See [decompilation options and limits](docs/DECOMPILATION.md). The traditional
`-convert -format map`, `map_bp`, and `map_220` options remain supported.

## Implemented capabilities

- Persistent workers, adaptive dispatch, named JSON pass profiles, optimized VIS
  bitsets/scratch storage, and `-vis -reproducible` for repeatable parallel results.
- Repeatable lighting jobs and bounce publication, conservative light-sample
  culling, safe polygon-light scratch storage and repaired floodlight sampling.
- Spatially indexed CPU minimaps and actual OpenCL GPU sampling, device inventory,
  explicit backend selection, deterministic random samples and automatic fallback.
- BSP/PRT range, reference and geometry checks; [strict numeric options](docs/CLI-INPUT.md); atomic BSP
  replacement and safer diagnostics.
- [MAP brush, patch and entity validation](docs/MAP-INPUT.md), bounded iterative
  script includes, safe degenerate-side handling and preservation of previous
  geometry sidecars on source parse errors.
- Indexed Raven lightgrid packing, bounded BSP inspection and a shared CLI/GUI
  catalog of 25 profiles: 19 native writers and six recovery-only readers.
- Decompilation with improved texture matching and UV reconstruction, preserved
  brush entities/origins/patches, Valve 220 output and a JSON recovery report.
- [Retained original patch paint](docs/PATCH-SOURCE.md) and bounded
  [triangle-to-patch reconstruction](docs/TRIANGLE-PATCH-RECOVERY.md), with separate
  source/inference provenance, compiled alpha/RGBA policies and rebuild checks.
  The [workbench patch page](docs/WORKBENCH.md#patch-recovery) provides saved
  settings, capability checks and a bounded recovery report review.
- [Multi-triangle UV fitting](docs/UV-RECOVERY.md) with seam/uncertainty diagnostics,
  precise full texture offsets and patch controls, bounded work and an explicit
  compatibility policy for previous texture recovery.
- Native Alice, F.A.K.K.2, Allied Assault and early Quake III recovery; terrain
  meshes, retained model placements and inference of missing early face materials.
- OBJ/ASE exports with parallel curve tessellation, preserved entity placement
  and configurable detail, available from both CLI and workbench.
- Conservative post-LIGHT triangle reduction with material/runtime guards,
  shared-index protection, deterministic jobs and separate BSP/report outputs.
- Qt projects, quality presets, asynchronous queues, cancellation, searchable logs,
  diagnostics, reports, build history, hardware discovery and light/dark themes.
- Separate CLI, optional Qt-free and GPU-disabled builds, portable runtime/source
  packaging, and asset-independent regression fixtures.
- Experimental per-face/per-patch lightmap density with a maintained NRC editor
  integration, versioned MAP data and requested-spacing grid controls.
- Experimental [patch RGBA compiler support](docs/PATCH-PAINT.md), with explicit
  material-color/vertex-light modes, paint-preserving triangles and relight checks.
  The companion Radiant panel adds RGB/alpha brushes, fill/reset, selection masks,
  undo, a raw color preview and a [static material camera preview](docs/PATCH-MATERIAL-PREVIEW.md)
  with explicit neutral lighting. Broader lighting/runtime qualification remains open.

Measured whole-command results on the documented Windows fixture: indexed CPU
minimaps were **29.8x faster** than the imported sampler; a sufficiently large GPU
minimap was **1.93x faster** than the optimized CPU; single-worker VIS used **12.1%
less elapsed time** on the alternating grid=9 test. CPU lighting culling reduced
elapsed time by **22.3%** at 20 workers on the dense material fixture, with identical
lighting data. These are workload-specific results. Lighting defaults to the CPU;
[experimental GPU area factors](docs/GPU-LIGHTING.md) are available for comparison
but have not improved measured complete-bake times. See
[measurements, hardware and reproduction details](docs/PERFORMANCE.md).

Indexed Raven packing reduced the complete identity-scale rewrite of Jedi
Academy's `duel9` from 8.54 to 0.27 seconds (**31.6x**), with identical lump
payloads. That measures loading, validation and serialization; it does not measure
the original lighting bake. The [coverage ledger](docs/GAME-COVERAGE.md) records
native validation of 242 installed-map entries without distributing game assets.

## Documentation

- [Changelog](CHANGELOG.md) and [release procedure](docs/RELEASE.md#publishing-a-release)
- [Implementation plan and acceptance criteria](docs/PLAN.md)
- [Architecture and decisions](docs/ARCHITECTURE.md)
- [Development and validation](docs/DEVELOPMENT.md)
- [Game profiles, format coverage and native-map evidence](docs/GAME-COVERAGE.md)
- [Asset-independent BSP inspection](docs/BSP-INSPECTION.md)
- [BSP geometry, regional costs and visibility evidence](docs/BSP-EVIDENCE.md)
- [Baked lighting observations and source-recovery limits](docs/LIGHTING-EVIDENCE.md)
- [Direct lighting probes and proposed-light evaluation](docs/LIGHT-PROBES.md)
- [Planned Radiant patch RGB/alpha painting and surface density previews](docs/PLAN.md#m11--radiant-painting-and-per-surface-lighting-controls)
- [Radiant density authoring, companion editor and preview limits](docs/RADIANT-AUTHORING.md)
- [Patch paint source format, compiler behavior and remaining editor work](docs/PATCH-PAINT.md)
- [VIS modes, merge repairs and compact working data](docs/VIS.md)
- [Measured performance and backend options](docs/PERFORMANCE.md)
- [Installation, packaging and known limits](docs/RELEASE.md)
- [BSP decompilation design](docs/DECOMPILATION.md)
- [Detail/group inference and the light/recreation roadmap](docs/RECOVERY-INFERENCE.md)
- [Planned intelligent VIS and geometry optimization](docs/COMPILER-OPTIMIZATION.md)
- [Exact planar reduction core and remaining renderer gates](docs/PLANAR-REDUCTION.md)
- [Geometry optimizer, renderer contract and validation](docs/GEOMETRY-OPTIMIZATION.md)
- [Upstream provenance and credits](docs/UPSTREAM.md)
- [Completed tasks and known issues](docs/PROGRESS.md)

## License and credits

q3mapx is distributed under GPL-3.0-or-later; see [COPYING](COPYING). The imported
compiler permits GPL-2.0-or-later, and its Apache-2.0 ETC component requires choosing
GPLv3 for the combined work. Imported files retain their original copyright and
license notices. See [licensing details](docs/UPSTREAM.md).

The compiler builds on the work of id Software, GtkRadiant, NetRadiant,
[Garux and the NRC contributors](https://github.com/Garux/netradiant-custom),
and q3map2's contributors including ydnar. See [provenance](docs/UPSTREAM.md).
The optional Radiant authoring patch also builds directly on NRC's editor model,
serialization and renderer; [its pinned manifest](integrations/nrc/manifest.json)
records the modified upstream files and their original hashes.

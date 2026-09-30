# q3mapx

An independent, performance-focused continuation of the q3map2 compiler from
[NetRadiant-custom (NRC)](https://github.com/Garux/netradiant-custom), with a native
desktop workbench and the existing command-line workflow.

q3mapx 0.3.0 is a development release with persistent CPU jobs, optional OpenCL
minimap acceleration, stronger input validation, improved BSP recovery, and a
native Qt workbench. It starts from NRC revision `8216133` (latest at retrieval on
2026-09-29). See the [implementation plan](docs/PLAN.md) and [task log](docs/PROGRESS.md)
for completed work, validation and remaining limits.

The standalone CLI builds with CMake/Ninja; see [build instructions](docs/DEVELOPMENT.md).
Release output is `build/release/bin/q3mapx` (`.exe` on Windows).
The native GUI is `build/release/bin/q3mapx-workbench`; see the [workbench guide](docs/WORKBENCH.md).
For the portable Windows archive, dependencies, source and installation steps,
see [release packaging](docs/RELEASE.md).

Development after 0.3.0 adds checked, staged MAP/report and mesh output with
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
Optional [detail and group inference](docs/DECOMPILATION.md#group-inference-policy)
can now export cell-supported detail flags and surface-supported `func_group`
assemblies, with bounded work, rebuild-order protection and explicit uncertainty.
The packaged 0.3.0 archive remains unchanged.

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
- BSP/PRT range, reference and geometry checks; strict numeric options; atomic BSP
  replacement and safer diagnostics.
- Indexed Raven lightgrid packing, bounded BSP inspection and a shared CLI/GUI
  catalog of 25 profiles: 19 native writers and six recovery-only readers.
- Decompilation with improved texture matching and UV reconstruction, preserved
  brush entities/origins/patches, Valve 220 output and a JSON recovery report.
- Native Alice, F.A.K.K.2, Allied Assault and early Quake III recovery; terrain
  meshes, retained model placements and inference of missing early face materials.
- OBJ/ASE exports with parallel curve tessellation, preserved entity placement
  and configurable detail, available from both CLI and workbench.
- Qt projects, quality presets, asynchronous queues, cancellation, searchable logs,
  diagnostics, reports, build history, hardware discovery and light/dark themes.
- Separate CLI, optional Qt-free and GPU-disabled builds, portable runtime/source
  packaging, and asset-independent regression fixtures.

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

- [Implementation plan and acceptance criteria](docs/PLAN.md)
- [Architecture and decisions](docs/ARCHITECTURE.md)
- [Development and validation](docs/DEVELOPMENT.md)
- [Game profiles, format coverage and native-map evidence](docs/GAME-COVERAGE.md)
- [Asset-independent BSP inspection](docs/BSP-INSPECTION.md)
- [BSP geometry, regional costs and visibility evidence](docs/BSP-EVIDENCE.md)
- [VIS modes, merge repairs and compact working data](docs/VIS.md)
- [Measured performance and backend options](docs/PERFORMANCE.md)
- [Installation, packaging and known limits](docs/RELEASE.md)
- [BSP decompilation design](docs/DECOMPILATION.md)
- [Detail/group inference and the light/recreation roadmap](docs/RECOVERY-INFERENCE.md)
- [Planned intelligent VIS and geometry optimization](docs/COMPILER-OPTIMIZATION.md)
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

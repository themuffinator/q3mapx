# q3mapx

An independent, performance-focused continuation of the q3map2 compiler from
[NetRadiant-custom (NRC)](https://github.com/Garux/netradiant-custom), with a native
desktop workbench and the existing command-line workflow.

q3mapx 0.1.0 is a development release with persistent CPU jobs, optional OpenCL
minimap acceleration, stronger input validation, improved BSP recovery, and a
native Qt workbench. It starts from NRC revision `8216133` (latest at retrieval on
2026-09-29). See the [implementation plan](docs/PLAN.md) and [task log](docs/PROGRESS.md)
for completed work, validation and remaining limits.

The standalone CLI builds with CMake/Ninja; see [build instructions](docs/DEVELOPMENT.md).
Release output is `build/release/bin/q3mapx` (`.exe` on Windows).
The native GUI is `build/release/bin/q3mapx-workbench`; see the [workbench guide](docs/WORKBENCH.md).
For the portable Windows archive, dependencies, source and installation steps,
see [release packaging](docs/RELEASE.md).

Recover an editable map with a texture-recovery report:

```sh
q3mapx -decompile -game quake3 -fs_basepath /path/to/game -o recovered.map example.bsp
```

See [decompilation options and limits](docs/DECOMPILATION.md). The traditional
`-convert -format map`, `map_bp`, and `map_220` options remain supported.

## Implemented capabilities

- Persistent workers, adaptive dispatch, named JSON pass profiles, optimized VIS
  bitsets/scratch storage, and `-vis -reproducible` for repeatable parallel results.
- Spatially indexed CPU minimaps and actual OpenCL GPU sampling, device inventory,
  explicit backend selection, deterministic random samples and automatic fallback.
- BSP/PRT range, reference and geometry checks; strict numeric options; atomic BSP
  replacement and safer diagnostics.
- Decompilation with improved texture matching and UV reconstruction, preserved
  brush entities/origins/patches, Valve 220 output and a JSON recovery report.
- Qt projects, quality presets, asynchronous queues, cancellation, searchable logs,
  diagnostics, reports, build history, hardware discovery and light/dark themes.
- Separate CLI, optional Qt-free and GPU-disabled builds, portable runtime/source
  packaging, and asset-independent regression fixtures.

Measured whole-command results on the documented Windows fixture: indexed CPU
minimaps were **29.8x faster** than the imported sampler; a sufficiently large GPU
minimap was **1.93x faster** than the optimized CPU; single-worker VIS used **12.1%
less elapsed time** on the alternating grid=9 test. These are workload-specific
results, not general BSP/LIGHT speedup claims. Lighting remains CPU based. See
[measurements, hardware and reproduction details](docs/PERFORMANCE.md).

## Documentation

- [Implementation plan and acceptance criteria](docs/PLAN.md)
- [Architecture and decisions](docs/ARCHITECTURE.md)
- [Development and validation](docs/DEVELOPMENT.md)
- [Measured performance and backend options](docs/PERFORMANCE.md)
- [Installation, packaging and known limits](docs/RELEASE.md)
- [BSP decompilation design](docs/DECOMPILATION.md)
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

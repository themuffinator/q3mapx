# q3mapx

An independent, performance-focused continuation of the q3map2 compiler from
[NetRadiant-custom (NRC)](https://github.com/Garux/netradiant-custom), with a native
desktop workbench and the existing command-line workflow.

The project is in active development. Planned features are described in the
[implementation plan](docs/PLAN.md); they are not claims of completed functionality.
See the [task log](docs/PROGRESS.md) for implemented and validated work.

## Objectives

- Faster BSP, visibility, lighting, conversion, and minimap workflows, backed by measurements.
- Persistent CPU jobs and GPU acceleration for workloads that benefit from it, with CPU fallback.
- Safer handling of malformed maps, BSPs, assets, and command-line options.
- Better BSP decompilation, including texture reconstruction and useful recovery diagnostics.
- A responsive Qt desktop workbench for projects, presets, build queues, diagnostics, and decompilation.
- Continued support for q3map2 command-line options and supported game formats.

## Documentation

- [Implementation plan and acceptance criteria](docs/PLAN.md)
- [Architecture and decisions](docs/ARCHITECTURE.md)
- [Development and validation](docs/DEVELOPMENT.md)
- [BSP decompilation design](docs/DECOMPILATION.md)
- [Upstream provenance and credits](docs/UPSTREAM.md)
- [Completed tasks and known issues](docs/PROGRESS.md)

## License and credits

q3mapx follows q3map2's GPL-2.0-or-later licensing. Imported components retain their
individual copyright notices and compatible licenses. The source import will
include the upstream license texts and contributor list.

The compiler builds on the work of id Software, GtkRadiant, NetRadiant,
[Garux and the NRC contributors](https://github.com/Garux/netradiant-custom),
and q3map2's contributors including ydnar. See [provenance](docs/UPSTREAM.md).

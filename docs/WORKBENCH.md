# Desktop workbench

Run `q3mapx-workbench` (`.exe` on Windows), beside the `q3mapx` compiler. The CLI
remains a separate executable and can be built without Qt using
`-DQ3MAPX_BUILD_GUI=OFF`.

## First project

1. Choose a source `.map` for compilation or `.bsp` for recovery/processing.
2. Set the game root that contains `baseq3` or the corresponding game's asset
   directory, then select the game profile. Set a mod directory if needed.
3. Choose an output folder and confirm the compiler path.
4. Choose quality, workers and a workflow. GPU settings apply to minimaps.
5. Inspect the command preview. Press **F5** to queue and run, or add several
   workflows before starting the queue.
6. Review stage results, searchable logs, diagnostics and JSON reports. Open the
   output folder to use the resulting BSP, recovered MAP, OBJ/ASE mesh, minimap or profiles.

Every workflow stages an independent copy of its input in a unique run directory.
It does not compile over the original source/BSP. A full build runs BSP, VIS and
LIGHT in sequence; a failed or cancelled stage skips its dependent stages.
Independent queued workflows can continue after a failure. Cancel stops the
current process and pauses the queue. Start queue resumes remaining work.
Visibility-only jobs require a matching `.prt` beside their source BSP.

The game list comes from the selected compiler's `-games` catalog. It includes
the compiler's complete set of profiles, accepts their aliases, and shows the
native format and default asset folder. **Refresh** queries a replaced compiler
again. Queries run in the background with bounded output and a timeout; starting
a workflow waits for discovery and rejects unsupported profile/workflow pairs.
An older compiler without a catalog still permits a manually entered profile,
with its discovery failure shown beside the selection.

## Controls

- Project: source/assets/output/compiler paths, game/mod, saved JSON projects.
- Quality: draft, balanced and production presets; explicit CPU worker count.
- Reproducible visibility: enabled for new projects; fixed publication batches
  produce repeatable VIS across worker counts at some scheduling cost. Older
  saved projects without this setting retain their previous behavior.
- Minimap: automatic/CPU/GPU/reference backend, device index, size and samples.
- Recovery: Valve 220, brush primitives or classic coordinates; automatic loss report.
- Mesh export: OBJ/MTL or ASE, including curves and native MOHAA terrain. Curve
  detail is 1–32 samples per span (default 8); higher values create larger meshes.
- Advanced: separate extra arguments for BSP, VIS and LIGHT, one argument per
  line. Arguments with spaces remain intact; do not add shell quotes.
- Build queue: stage status, elapsed time, logs, diagnostics and generated reports.
- History: the latest 100 completed runs, their output folders and reloadable
  project snapshots. The snapshot references the staged source copy.
- Hardware: asynchronous JSON device inventory from the configured compiler.
- View: light/dark themes. Standard Qt focus navigation and label mnemonics apply.

**Ctrl+N/O/S** create/open/save projects, **Ctrl+Shift+S** saves a copy,
**Ctrl+Enter** queues a workflow, **F5** runs it, and **Shift+Escape** cancels the
active job. Logs are bounded in the live view; complete child output stays on disk.

Projects use schema version 1, store argument arrays rather than shell commands,
and reject invalid field types, unknown schema versions and invalid numeric limits.
Relative paths in a project resolve against the project file's directory. Saves
use atomic replacement. GUI preferences/history normally live in the OS application
configuration directory; `--state-dir PATH` selects an isolated location.

## Validation and limits

Automated tests run the real compiler through the Qt queue, including complete
builds, recovery, OBJ/ASE exports, minimaps, paths with spaces, failed process starts, dependency
skipping and cancellation. An offscreen window test also discovers the compiler's
profiles and invokes its build action directly, without input injection. Catalog
tests exercise malformed replies, output limits, timeouts and stale responses.
The window test also checks recovery-only mesh capabilities and curve-detail
command construction, and directly renders the quality controls without input
events. Mesh export limitations are in the [recovery guide](DECOMPILATION.md).
The GUI can render its own widget tree directly to PNG:

```sh
q3mapx-workbench -platform offscreen --project project.q3mapx.json --state-dir build/ui-state --render-preview build/workbench.png
```

This uses Qt painting into an image, not operating-system screen capture, and
does not inject mouse/keyboard input. Small layouts use scrolling form pages;
controls and text follow Qt's DPI scaling. Manual assistive-technology testing
and broad real-game project testing are still needed before a production release.

The workbench is a compiler/recovery application, not a map editor. BSP recovery
limits are described in [DECOMPILATION.md](DECOMPILATION.md). Lighting defaults to
CPU. Experimental GPU area factors can be selected through extra LIGHT arguments
(`-light-backend` and `gpu` on separate lines); see [limits and measurements](GPU-LIGHTING.md).
The regular backend/device controls still apply to minimaps. Legacy compiler fatal exits are surfaced through process status/logs even
when no CPU profile was produced.

# Desktop workbench

Run `q3mapx-workbench` (`.exe` on Windows), beside the `q3mapx` compiler. The CLI
remains a separate executable and can be built without Qt using
`-DQ3MAPX_BUILD_GUI=OFF`.

## First project

1. Choose a source `.map` for compilation or `.bsp` for recovery/processing.
2. Set the game root that contains `baseq3` or the corresponding game's asset
   directory, then select the game profile. Set a mod directory if needed.
3. Choose an output folder and confirm the compiler path.
4. Choose quality, workers and a workflow. GPU settings apply to minimaps;
   **Recovery** contains MAP format, brush order, detail flags and group inference.
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
again. Queries run in the background with a 1 MiB combined stdout/stderr limit
and a ten-second timeout; starting
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
- Recovery tab: Valve 220, brush primitives or classic coordinates, saved BSP/rebuild
  brush order, optional detail/group inference and expandable analysis work limits.
  Recovery produces a JSON report of decisions, assumptions and limitations.
- Mesh export: OBJ/MTL or ASE, including curves and native MOHAA terrain. Curve
  detail is 1–32 samples per span (default 8); higher values create larger meshes.
- Advanced: separate extra arguments for BSP, VIS and LIGHT, one argument per
  line. Arguments with spaces remain intact; do not add shell quotes.
- Build queue: stage status, elapsed time, logs, diagnostics and generated reports.
- History: the latest 100 completed runs, their output folders and reloadable
  project snapshots. The snapshot references the staged source copy.
- Hardware: a GPU device table with vendor, memory and compute-unit counts;
  selected-device details show the OpenCL version and whether memory is shared
  with the host. The full validated inventory remains available in a JSON tab.
- BSP inspection: choose any BSP, inspect its format and named file sections,
  compare ambiguous directory layouts, and save the full JSON report. The
  optional profile check uses the project's selected game. The inspection file
  is independent of the project source; no game assets are needed.
- View: light/dark themes. Standard Qt focus navigation and label mnemonics apply.

**Ctrl+N/O/S** create/open/save projects, **Ctrl+Shift+S** saves a copy,
**Ctrl+Enter** queues a workflow, **F5** runs it, and **Shift+Escape** cancels the
active job. Logs are bounded in the live view; complete child output stays on disk.
Report previews show up to 5 MiB and say so below the preview; complete JSON files
remain in the run folder.

Projects use schema version 1, store argument arrays rather than shell commands,
and reject invalid field types, unknown schema versions and invalid numeric limits.
Relative paths in a project resolve against the project file's directory. Saves
use atomic replacement. GUI preferences/history normally live in the OS application
configuration directory; `--state-dir PATH` selects an isolated location.

Development builds after 0.3.0 offer **BSP order** (the unchanged default) and
**Rebuild order** in the Recovery tab. Rebuild order can reduce partition changes
when a recovered MAP is recompiled with matching assets/settings; it does not
recover original editor ordering or guarantee identical visibility. See
[ordering behavior and validation](DECOMPILATION.md#brush-order-for-rebuilding).

The compiler catalog must advertise rebuild order for a profile with BSP writing
support. Older compilers and native recovery-only profiles keep ordinary BSP
order available. A saved rebuild choice remains visible when switching to an
incompatible compiler/profile, with an explanation; it is not silently replaced.
Run, queue and menu actions reject that combination before staging inputs.
The project schema's optional `brush_order` field accepts `bsp` or `rebuild`.
Projects without it retain BSP order and the previous command; only decompile
jobs receive the new CLI argument, and run snapshots retain the choice.

The Recovery tab now also offers **Infer from brush interiors** for detail flags
and **Infer func_group assemblies** for grouping. They are independent choices:
groups can contain both detail and structural brushes. Legacy flags and flat
world geometry remain the defaults. Group inference selects Rebuild order and
disables BSP order until grouping is turned off; turning grouping off retains
Rebuild order, which can then be changed normally. These are proposals with
explicit uncertainty, not recovery of uniquely proven authoring metadata. See
[detail behavior](DECOMPILATION.md#detail-inference-policy) and
[group behavior](DECOMPILATION.md#group-inference-policy).

**Analysis work limits** expands separate budgets for detail and group analysis.
Each defaults to 50,000,000 work units and accepts 1–100,000,000. These are bounded
analysis-operation counts, not time estimates. Exhaustion fails the recovery job;
previous MAP/report outputs are preserved. Controls are enabled only for active,
supported policies. Budgets remain saved while their policies are off, but only
active inference arguments reach decompile jobs. Other workflows receive none.

The selected compiler must advertise each inference policy for the selected
profile. Grouping also requires advertised rebuild-order support. Older catalogs
without inference metadata cannot enable it just because the profile can write
BSPs. Saved choices remain visible across incompatible compiler/profile changes;
guidance explains how to return to supported settings, and run/menu/queue actions
reject incompatible inference before staging any input. Switching back restores
availability. Full-size windows show expanded limits together; the compact view
keeps the main choices visible and allows scrolling through expanded limits.

Schema 1 adds optional `detail_policy` (`legacy` or `cells`), `group_policy` (`none`
or `surfaces`), `detail_work_limit` and `group_work_limit` fields. Projects without
them keep legacy/flat behavior and default budgets. Invalid types, policies and
out-of-range budgets are rejected. A saved surface-group choice requires
`brush_order: "rebuild"`; the UI maintains this relationship automatically.

The inspection page is available in development builds after 0.3.0. It shows
invalid-directory diagnostics as well as successful results and supports cancelling
a query. A resizable divider separates the section table from diagnostic notes.
Queries have a ten-second deadline and a 1 MiB combined output limit; stale or
malformed replies cannot replace the current result. Changing the file, compiler
or enabled profile check clears the previous report. Saving uses atomic replacement
and refuses to replace the inspected BSP. Directory checks do not validate geometry
or establish game compatibility; see [inspection scope](BSP-INSPECTION.md).

The updated hardware page is also available in development builds after 0.3.0.
Use **Refresh device inventory** to query the selected compiler, including after
replacing it at the same path. **Cancel query** stops discovery; changing the
compiler clears its previous result and cancels an active query. Late replies
cannot replace the current inventory. Queries have a fifteen-second deadline
and a 1 MiB combined stdout/stderr limit. Validated inventories contain at most
1,024 devices; malformed fields, duplicate indices, failed starts, nonzero exits
and crashes produce a diagnostic instead of device rows. Compiler messages retain
their first 8 KiB. No available OpenCL GPU is a valid result: CPU workflows remain
available, with the compiler's reason shown in the details pane.

Device indices match the compiler's `-devices` output and can be entered under
**Project → Quality & compute** for minimaps. Memory is driver-reported global
memory, which can include shared host memory. Compute-unit counts are not a
performance ranking across vendors. Different OpenCL implementations may list
the same physical GPU more than once; the page preserves these separate indices.

## Validation and limits

Development builds also provide **Analyze geometry · Quake3e GL** and **Optimize
geometry · Quake3e GL** workflows for final baked Quake III BSPs. Matching shader
assets and the advertised compiler/profile capability are required. The command
preview explains the initial material constraints and selects `quake3e-gl` explicitly.
Analysis writes `geometry.json`; optimization adds `<basename>.optimized.bsp` in
the new run folder. The staged source is retained separately. View surface
decisions under **Reports & profiles**. Other renderer contracts, interactive
region review and GUI exclusions remain planned; the CLI already accepts exact
surface/shader exclusions and an analysis budget. See [the optimizer guide](GEOMETRY-OPTIMIZATION.md).

Automated tests run the real compiler through the Qt queue, including complete
builds, recovery, OBJ/ASE exports, minimaps, paths with spaces, failed process starts, dependency
skipping and cancellation. An offscreen window test also discovers the compiler's
profiles and invokes its build action directly, without input injection. Catalog
tests exercise malformed replies, exact combined-output boundaries, overflow on
either stream, timeouts, stale responses and cleanup of superseded child processes.
Inference tests also recover two known source assemblies through the queue, check
the compiler's actual policy/budget report and run snapshot, and exercise saved
settings and absent/malformed capability metadata. Direct offscreen widget actions
verify old/current compiler switches, retained choices, rejection before directory
creation and light/dark rendering at both supported window sizes. These settings
do not yet provide proposal overlays, saved per-brush overrides or an interactive
authoring reconstruction review. See [validation evidence](validation/workbench-inference-controls.json).
The window test also checks recovery-only mesh capabilities and curve-detail
command construction, and directly renders the quality controls without input
events. Mesh export limitations are in the [recovery guide](DECOMPILATION.md).
Inspector tests cover real native directory reports, malformed schemas, oversized
output on either stream, timeouts, cancellation, failed starts and superseded
queries. The window test switches ambiguous layouts, saves reports, checks source
protection and paints both standard and compact inspection layouts.
Device tests cover real and GPU-free replies, 27 malformed-schema cases, exact
and excessive output bounds on both streams, diagnostics, timeouts, crashes,
cancellation, failed starts and supersession. The window test checks device
selection, compiler changes and cancellation, then paints standard and compact
hardware layouts in both themes. [Recorded validation](validation/hardware-inventory.json)
includes Windows and Linux results and the generated preview locations.
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

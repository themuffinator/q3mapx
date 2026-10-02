# Compiled patch color recovery

For BSPs built with retained authoring data, use
[`-patch-recovery source`](PATCH-SOURCE.md) to restore pre-modifier controls and
settings, including triangle-only sources. The compiled-channel policy below
remains useful when no source archive exists.

For eligible triangle-only meshes, combine this color choice with
[`-patch-recovery fit|auto`](TRIANGLE-PATCH-RECOVERY.md). Fitted fields are checked
against compiled triangle samples and reported separately from native controls.

MAP export can explicitly retain compiled alpha or RGBA from surviving native
Bezier patch controls. This is extraction of stored channels, **not identification
of the author's original paint**. Ordinary BSP vertex RGB may be baked lighting;
the BSP/SRF paint binding does not archive the original controls or settings.

```sh
q3mapx -game quake3 -fs_basepath /path/to/game -decompile -patch-colors alpha -o rebake.map maps/example.bsp
q3mapx -game quake3 -fs_basepath /path/to/game -decompile -patch-colors rgba -patch-color-subdivisions 8 -o colors.map maps/example.bsp
```

| Option | Output |
| --- | --- |
| `-patch-colors none` | Default, unchanged `patchDef2` export without RGBA. |
| `-patch-colors alpha` | Stored alpha, white source RGB, `vertexRGB lighting` for rebaking. |
| `-patch-colors rgba` | Stored RGBA, `vertexRGB material`; this freezes any baked RGB as material color. |
| `-patch-color-subdivisions N` | Chosen output sampling per quadratic span: 1, 2, 4, 8, 16 or 32, default 16. Requires alpha or RGBA recovery. |

Eligible patches become `q3mapxPatchDef2`, readable by the matching q3mapx compiler
and companion Radiant integration. All three MAP projection formats are supported,
as are `-convert` and `-fast`. Recovered controls always use round-trip float
precision, including with the legacy triangle UV policy. Native model origins
are applied to positions and absolute stored patch UVs are retained.

The chosen subdivisions are **not** an inferred original quality setting. Existing
geometric refinement may increase sampling. `lightmapSampleSize 0` inherits normal
compile settings because an original per-patch density is not established here.
Neither the adjacent MAP nor SRF is loaded for this extraction. A BSP-writing game
profile and compiled BSP input are required; material replacement with `-wtf` is
incompatible. Dedicated workbench controls for these options remain planned.

## Channel and rendering limits

Extraction reads native control color slot zero. An active secondary RBSP vertex
style must have identical requested channels at every control. Alpha recovery can
therefore proceed when only RGB differs; RGBA recovery cannot. Inactive style
slots do not constrain recovery. This check does not restore the original style
configuration or promise equivalent animated lighting after a rebuild.

Painted output uses finite triangle tessellation to retain the color field.
Converting an ordinary native patch changes its render topology and LOD behavior.
The extracted bytes and Bezier field do not establish pixel-equivalent rendering,
seam behavior, or equivalent rebaked illumination. Compare the rebuilt map with
the source BSP before relying on its presentation.

Solid painted patches often retain a nodraw native collision grid. Its compiled
channels may survive even though the visible surface is a separate triangle mesh.
Modifiers, tessellation and lighting can make those controls differ from the
rendered mesh's colors. Non-solid painted patches can have no native grid at all;
this exporter does not fit new control grids to triangle soup. Original
pre-modifier paint, RGB mode, density and sampling settings remain unproven even
when a paint binding is present. Keep the original MAP whenever possible.

## Conservative fallback

Ineligible patches retain the ordinary colorless `patchDef2` representation.
Their reasons are recorded individually; other eligible patches still recover.
Current shader assets affect eligibility, so use the intended game and asset paths.

| Report status | Reason |
| --- | --- |
| `recovered` | Requested stored channels were written to the MAP. |
| `conflicting_active_styles` | A single source color field cannot retain all active requested channels. |
| `paint_grid_limit` | Dimensions or requested refinement exceed the painted-grid limits. |
| `incompatible_paint_material` | Indexed blending or autosprite semantics conflict with paint. |
| `material_color_modifier` | A current shader would apply color/alpha modifiers again. |
| `material_geometry_or_uv_modifier` | Current shader offset, inversion, texture-coordinate transforms, fur, terrain, surface models, foliage, clones, back surfaces or remapping could alter the result again. |
| `material_redirect` | Shader deprecation resolves the stored name to another material. |
| `unsupported_material_name` | The stored name is outside the patch parser's `textures/` namespace. |
| `color_modifier_volume_present` | A surviving brush material contains a color-modifier volume. Recovery conservatively skips all patch colors, without assuming spatial independence. |
| `source_coordinate_limit` | A position after model-origin translation cannot be represented within MAP coordinate limits. |

These checks detect known operations in the **current** material definitions.
Missing/changed assets, removed modifier volumes and discarded source entities or
compile settings cannot be recovered by an absence-of-modifiers check. A skipped
patch still has the ordinary decompiler's geometry/material reconstruction limits;
fallback does not certify rebuild equivalence.

## Recovery report and publication

Selecting alpha or RGBA always writes the normal recovery JSON, including when
using `-convert`. The additive `patch_colors` object in schema version 1 records
the policy, compiled-control basis, source slot, chosen output settings, aggregate
status counts and per-surface decisions. It sets `original_paint_proven` and
`rebuild_equivalence_proven` to false. Comments beside recovered MAP patches also
identify the channels as compiled data.

At most 10,000 per-patch decisions are stored; `omitted_records` counts additional
decisions. Aggregate counts and MAP output remain complete. The existing 64 MiB
inference-report limit also applies. MAP and report use the existing checked,
staged publication with rollback; this does not add a two-file power-loss or
concurrent-writer guarantee. Invalid options fail before publication.

The [workbench patch page](WORKBENCH.md#patch-recovery) now exposes alpha/RGBA
and native sampling controls, saves them in project/run snapshots and reviews
native decisions separately from retained sources and inferred triangle grids.

## Verification

```sh
ctest --test-dir build/release -R '^(patch_color_recovery|patch_paint|decompile_recovery|recovery_outputs|uv_recovery)$' --output-on-failure -j 2
python tests/patch_color_recovery.py --compiler build/release/bin/q3mapx --reference path/to/previous/q3mapx --work-dir build/release/tests/patch-color-recovery
```

Use `.exe` on Windows. The generated Q3/JA corpus covers ordinary, material and
lighting patches before/after LIGHT, flat multi-span and curved grids, translated
brush models, three MAP formats, six quality settings, exact control extraction,
independent Bernstein sample evaluation and repeated LIGHT. Poisoned adjacent
MAP/SRF files establish that extraction uses the BSP alone. Default exports are
compared byte-for-byte, optionally against the preceding compiler. Guards cover
material replay, active/inactive color styles, large grids, triangle-only paint,
report truncation, malformed CLI options and failed publication. See the
[recorded platforms and counts](validation/patch-color-recovery.json). These are
compiler-data tests; runtime pixels and native editor interaction are not measured.

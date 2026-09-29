# BSP decompilation

## Intent

Recover editable maps from supported BSPs with better texture alignment, faithful
brush entities and patches, robust validation, and useful diagnostics. Preserve
the existing `-convert -format map` family of workflows and expose a convenient
decompile action in the CLI and workbench.

## Limits of the input format

A compiled BSP does not preserve the original editor grouping, all source brush
boundaries, source model instances, or removed entities. Decompilation cannot
promise an identical source map. Report which data was reconstructed, approximated,
or unavailable. Do not fabricate source metadata.

## Planned work

1. Validate file headers, lump ranges, counts, model/surface/brush references,
   plane indices, triangle indices, and patch dimensions before conversion.
2. Fix texture candidate lookup. The imported spatial search must be checked for
   triangles whose maximum bound extends beyond a brush while still overlapping it.
3. Make UV reconstruction tolerate degenerate triangles, avoid non-finite results,
   and retain explicit fallback diagnostics.
4. Preserve origin offsets and entity properties, handle malformed model numbers,
   and validate patch control-point access.
5. Generate a recovery report with counts and warnings, and expose lossless versus
   approximate recovery clearly in the workbench.
6. Measure conversion performance on generated dense geometry before introducing
   spatial-index or parallel reconstruction changes.

## Usage

```sh
q3mapx -decompile -game quake3 -fs_basepath /path/to/game -o recovered.map maps/example.bsp
q3mapx -decompile -format map_bp -report recovery.json maps/example.bsp
q3mapx -convert -format map_220 maps/example.bsp
```

`-decompile` defaults to Valve 220 output and writes `<output.map>.recovery.json`.
Without `-o`, the map is `<input>_converted.map`. `-report` selects a different JSON
path. The legacy `-convert` syntax remains available and writes a report only when
explicitly requested. `-o` requires a `.map` extension and `-report` requires `.json`.
Unknown conversion formats/options now produce a diagnostic instead of silently
choosing ASE or ignoring the option.

Use the correct game profile and resource paths: shader dimensions affect texture
recovery. Valve 220 and brush primitives preserve affine texture mappings. Classic
Quake texture definitions cannot represent arbitrary shear; the report counts
such approximations. `-fast` skips texture reconstruction and reports fallback axes.

## Recovery details

A per-material bounds hierarchy searches all overlapping triangles, including
triangles larger than the source brush face. The previous maximum-bound cutoff
could miss these. Degenerate triangles are excluded. The affine solve works on
edge differences in double precision, rejects ill-conditioned geometry and
constant/non-finite UV axes, and supplies finite fallback transforms. Brush-detail
membership is collected once from leaf references.

The version 1 JSON report records entities, exported/skipped brushes, patches,
matched/fallback faces, degenerate triangles/transforms, approximate Quake UVs,
triangle-soup surface count, paths, output format, and recovery limitations.
Fallback faces include invisible brush sides that have no rendered triangle;
their count alone does not imply a visible defect. Model instances and editor
grouping cannot be reconstructed from data the BSP no longer contains.

## Acceptance fixtures

- Sealed room with rotated/scaled textures.
- Brush entity with a nonzero origin and preserved key/value pairs.
- Curved patch with valid control-point dimensions.
- Large triangle overlapping a small brush face.
- Degenerate triangles and finite fallback texture axes.
- Truncated header/lump, overflowing ranges, invalid plane/vertex/shader indices,
  and malformed entity model references.

Round-trip checks compare recoverable geometry, entity data, finite texture
coordinates, and successful recompilation. Exact byte equality is inappropriate
when compiler-generated ordering or metadata changes.

## Implemented validation

The loader checks headers and all loaded lump ranges before access, then verifies
cross-lump references, strings, finite geometry, patch dimensions, node graphs,
visibility dimensions, and Raven lightgrid indirection. Entity model references
are checked before conversion. `-force` cannot bypass these safety checks. Node
depth is capped at 1024 to protect legacy recursive traversals. A valid unaligned
lump is copied safely rather than rejected solely for its alignment.

Native Raven maps can carry non-finite lightmap UVs in slots their surfaces do
not use, including primary slots on vertex-lit patches. The loader checks every
referencing surface, then zeros only unused non-finite pairs and emits a warning.
Non-finite coordinates used by any surface still fail, including under `-force`.
Zero-geometry flares referring to fog 0 with an empty fog lump are repaired to no
fog; equivalent references on actual geometry are rejected. Recovery JSON records
these changes as `normalized_unused_lightmap_uv_pairs` and
`normalized_unused_flare_fogs`. Finite texture coordinates and active lightmap
coordinates are preserved.

## Native Alice and F.A.K.K.2 recovery

```sh
q3mapx -game alice -fs_basepath /path/to/Alice -decompile -o recovered.map input.bsp
q3mapx -game fakk2 -fs_basepath /path/to/FAKK2 -decompile -o recovered.map input.bsp
```

The asset roots contain `base` and `fakk`, respectively. Both CLI and workbench
offer recovery and minimaps for these native formats. BSP writing is disabled:
recover a standard MAP or export OBJ/ASE geometry instead of rewriting a native
file through an incompatible writer. Recovering into a different game's editor
still requires that game's entities/materials to be adapted by the user.

The mandatory MAP report includes `native_losses`, `native_shaders` (raw contents,
surface flags and shader subdivisions), and `native_surface_subdivisions` in
source surface order. These retain metadata outside standard MAP syntax. The
report explicitly identifies omitted native baked-light extensions and the
general limitations of source-light reconstruction. Native writing and lossless
round-tripping are not claimed. See [coverage and evidence](GAME-COVERAGE.md).

FTX image lookup follows the existing TGA/PNG/JPEG/DDS/KTX/CRN/WebP alternatives.
Its 12-byte header and RGBA payload are checked before allocation; dimensions
must be positive and at most 8192 per axis. Malformed images produce a diagnostic
and the usual missing-image fallback. Texture dimensions matter to Valve 220
scales, so installed assets or equivalent source textures should be available.

## Native Allied Assault recovery

```sh
q3mapx -game mohaa -fs_basepath /path/to/MOHAA -decompile -o recovered.map input.bsp
q3mapx -game mohaa -fs_basepath /path/to/MOHAA -convert -format obj input.bsp
```

The asset root contains `main`. The MAP contains recoverable brushes, entities
and Bezier patches. Terrain is available in OBJ/ASE export and the mandatory
JSON report, not as MAP brushes or patches. Native triangle hole flags are
respected. Static-model placements are retained in JSON; external TIKI meshes
are not imported. Native BSP writing and the brush-only minimap are disabled.

Additional report fields are `native_terrain`, `native_terrain_triangles`,
`native_terrain_removed_triangles`, `native_static_models`,
`native_side_equations`, `native_side_equation_indices`, and `fence_mask` in each
native shader. Terrain records contain world origin, height steps (two units
per step), texture corners, variance flags, shader index and lightmap metadata.
`native_surface_subdivisions` remains in original source surface order, excluding
the generated terrain meshes. Packed lighting and static-model color omissions
are named in `native_losses`.

`normalized_unused_native_equations` counts the narrow repair for zero equation
references in maps with neither fence contents nor an equation table. Invalid
active references are rejected. See [coverage and evidence](GAME-COVERAGE.md).

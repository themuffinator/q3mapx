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

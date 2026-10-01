# Patch RGBA authoring

q3mapx accepts experimental **RGBA paint on patch controls** through
`q3mapxPatchDef2`. Alpha does not require `alphaMod` brushes. RGB can remain normal
vertex lighting or become explicit material color. The compiler evaluates the
paint together with the patch and preserves it through BSP assembly and LIGHT.

The maintained [NRC integration](RADIANT-AUTHORING.md#prepare-and-build-the-companion-editor)
now provides native paint storage, a painting panel, undo and save/reopen support.
This is experimental M11 functionality. The panel previews raw RGBA in patch
parameter space and offers a [static material camera preview](PATCH-MATERIAL-PREVIEW.md)
with explicit neutral lighting. Broader lighting/runtime qualification remains
open. Use the matching editor and compiler with these versioned MAP primitives.

## Painting in Radiant

1. Select one Quake III patch, open the Surface Inspector and choose
   **q3mapx patch paint…**. Axial, brush-primitive and Valve 220 MAP profiles work.
2. Enable **RGB (material color)**, **Alpha**, or both. Pick RGB and/or an alpha
   byte. RGB authoring switches that patch to material mode, including when
   explicitly painting white; alpha-only authoring retains baked vertex RGB.
3. Drag on the parameter-space canvas. Radius is measured in normalized patch
   parameter units, strength is the maximum blend per stroke, and falloff ranges
   from a hard brush at zero to softer profiles at higher values. A control's
   strongest coverage wins within each stroke, so overlapping mouse events do
   not repeatedly accumulate opacity. Start another stroke to build up color.
4. Use Radiant's vertex component selection to restrict the editable controls;
   an empty component selection paints nothing. Primitive selection enables all
   controls. **Fill enabled channels** uses the exact chosen bytes, independently
   of brush strength; **Reset enabled channels** restores white/opaque values.
5. Choose segments per quadratic span and save the MAP. Choices exceeding the
   compiler's vertex limit are disabled. Compile and relight with q3mapx.

Each completed stroke/fill/reset is one native undo operation. Strokes edit an
independent preview until release; Escape, focus loss, hiding the panel or a
changed target cancels them. Commit rechecks patch identity, controls, settings
and selection mask. No patch pointer is retained after its node is released.
Painting default opaque alpha on an unpainted patch keeps its legacy representation.
Resetting all RGB/alpha returns to a density-only or ordinary patch primitive.
Reset RGB alone restores lighting mode only after every RGB control is white.

The checker displays alpha and the circles show authored controls. The smooth
field is the continuous quadratic control field, rounded to bytes; actual BSP
triangles approximate it at the chosen resolution. Control colors are generally
not interpolated through by the curve. This raw canvas does not evaluate material
stages. Enable **Preview selected patch material in camera** for supported static
Quake III textures, vertex channels, blending and depth with neutral lighting.
Unsupported materials show a reason and retain normal editor rendering. See the
[preview contract and limits](PATCH-MATERIAL-PREVIEW.md). Interactive brush
ergonomics remain unqualified; no automated mouse input is used.

MAP and native XML clipboard transfer preserve RGBA, mode and subdivisions,
alongside position/UV/density. XML uses an ordered `q3mapxPaint2` child after the
control matrix. Malformed paint in that child is diagnosed and leaves the current
paint unchanged; the inherited XML API cannot reject the whole document, so an
otherwise new patch can remain unpainted. MAP is the supported interchange format.
Cross-game XML migration and third-party topology plugins remain unqualified.

Duplication, undo, transpose and inversion retain control associations. Row/column
insertion subdivides geometry, UVs and paint together; new color controls round
once to bytes, so repeated insertions can accumulate quantization error. Painted
row/column removal now merges the first or last pair of quadratic spans **only
when the continuous geometry, UV and RGBA fields are exactly representable by
one quadratic**. Every affected line must pass; a failure changes nothing and
creates no undo memento. A candidate control must fit binary32 for geometry/UV
and a byte for color. There is no tolerance, byte clamping or approximate fit.
Quantized insertions are not necessarily reducible, even when undo would recover
the earlier patch. Cap creation remains guarded; reset paint on a copy to cap it.
Thickened opposite surfaces copy paint; wall color transport is implemented but
its native `NaturalTexture`/GL path still needs qualification. Arbitrary external
plugins which reconstruct patches can discard authoring metadata.

Reduction maps the two old half-spans to one complete span; unaffected spans keep
their controls. The normalized parameter positions of spans consequently change.
Paint subdivisions remain a per-span setting, so reduction can lower the number
of compiled samples/triangles and change their finite approximation. Exact source
field preservation is **not a runtime pixel-equivalence claim** or an automatic
compiler geometry optimization. Raise subdivisions if necessary for the intended
material. The native dimension setter clamps both axes before rounding them to
odd sizes, and an edit at a dimension limit never falls through to the other axis.

Adding RGBA to `PatchControl` changes its layout. The integration bumps the patch
module API to **version 2**. Rebuild the editor and all patch-using modules/plugins
together; do not mix these with an existing NRC installation's binaries.

## Source contract

The primitive has the same column-major control ordering as `patchDef2`:

```text
{
q3mapxPatchDef2
{
q3mapx/paint
( 3 3 0 0 0 )
lightmapSampleSize 8
vertexRGB material
paintSubdivisions 8
(
( ( 0 0 64 0 0 255 0 0 255 ) ( 0 64 64 0 1 255 0 0 128 ) ( 0 128 64 0 2 255 0 0 0 ) )
( ( 64 0 64 1 0 0 255 0 255 ) ( 64 64 96 1 1 0 255 0 128 ) ( 64 128 64 1 2 0 255 0 0 ) )
( ( 128 0 64 2 0 0 0 255 255 ) ( 128 64 64 2 1 0 0 255 128 ) ( 128 128 64 2 2 0 0 255 0 ) )
)
}
}
```

This is a single primitive to place inside an entity in a sealed MAP, with a
matching shader. Each control is `x y z s t r g b a`. RGBA values are one to
three decimal digits in `0..255`; signs, fractions, exponents and nonfinite values
are rejected. Patch dimensions remain odd integers in `3..31`. Existing position,
UV and [density](RADIANT-AUTHORING.md#editing-and-precedence) validation applies.
All three clauses above are mandatory and ordered. A zero density inherits.

| `vertexRGB` | Source RGB | LIGHT behavior |
| --- | --- | --- |
| `lighting` | Must be `255 255 255` at every control | Bake RGB normally; preserve authored alpha |
| `material` | Arbitrary RGB bytes, including explicit white | Preserve material RGB and alpha; lightmaps still bake normally |

`material` is replacement of vertex-light RGB with material data. It does not
multiply paint by vertex lighting. On a vertex-lit shader it displays the
authored RGB without baked vertex illumination. On a lightmapped shader, use a
stage that consumes vertex RGB and a lightmap stage for illumination. Alpha
requires `alphaGen vertex` and appropriate blend/depth rules to affect appearance.
The compiler does not rewrite runtime shaders or select their blending behavior.

The source bytes are native material-channel values, not gamma/exposure-encoded
lighting. Shader `q3map_colorMod`/`q3map_alphaMod` and volume modifiers run after
tessellation, in their existing order, and can scale or replace the paint.
`q3map_noVertexLight` continues to apply to lighting mode. Material mode keeps its
lightmap instead of substituting vertex RGB via LIGHT's approximation pass.
It preserves all four stored native color channels on RBSP; qualification below
uses ordinary, unstyled Q3/JA scenes. Styled/dynamic lighting combinations are
not yet qualified. Paint does not add a new radiosity reflectance model.

Ordinary `patchDef2` and density-only `q3mapxPatchDef1` keep their existing syntax
and behavior. Removing paint requires a deliberate conversion to those layouts;
changing only the primitive name leaves extra RGBA tokens and is rejected.

## Tessellation and cost

A geometrically flat patch can carry a nonlinear color field. Native patch LOD
and the inherited compiler simplifier do not guarantee retention of that field.
Painted patches therefore automatically use a **triangle representation for
rendering**, even without `-meta`/`-patchmeta`. Solid/playerclip patches retain
their native nodraw patch for collision, following the existing patchmeta path.
There is no engine extension or new BSP lump.

`paintSubdivisions` is one of `1`, `2`, `4`, `8`, `16`, `32`: the requested number
of segments along each quadratic span. Existing geometric subdivision can raise
it to as many as 16. The compiler evaluates position, UV, normal and RGBA at a
uniform parameter grid; RGBA uses the quadratic Bernstein weights in double
precision and rounds once to a byte. It does not repeatedly average bytes or
remove rows merely because their positions are collinear. RGB differences also
participate in painted-vertex deduplication and normal-smoothing reconstruction.
Lighting and material modes cannot merge into the same output surface.

The rendered result interpolates those samples linearly over triangles. It is a
finite approximation to the continuous Bezier paint field, **not a lossless
per-pixel representation**. Higher subdivision can improve gradients and curved
silhouettes at a higher geometry cost. One 3×3 patch at 8 segments produces 128
triangles before degenerate-triangle removal. Compiler output reports the grid
and initial triangle count. A patch is limited to 65,536 evaluated vertices;
the parser checks worst-case geometric refinement before output publication.

Indexed terrain blending and autosprite materials are rejected because they
assign incompatible meanings to alpha or geometry. Animated/deforming materials,
runtime LOD seams with neighboring native patches, fog boundaries, dynamic light,
large-map cost and broader native profiles still need qualification. Existing
coordinate-precision and meta-surface limits continue to apply. The inherited
minimum-edge heuristic is bypassed for paint: valid small patches must not
disappear just because refinement makes short edges. Zero-area triangles are
still removed. Source control
colors are values of a Bezier control field, not a promise that the curve passes
through every control color.

## Relighting and recovery

Keep the BSP and its matching SRF together. Each painted SRF surface stores
`patchPaintMode`; its default section stores `patchPaintBinding1`. The BSP
worldspawn contains `_q3mapx_patchPaint1` with the same SHA-256. The digest binds
surface ordering, parent references, ranges, topology, positions, UVs, modes and
the immutable paint channels. It excludes lightmap allocation, baked RGB and
generated shader names, which LIGHT can change. This is an identity check, not
an authenticity signature or a checksum of every editable SRF setting.

LIGHT checks the binding before removing its previous generated shader or
writing outputs. Missing, stale or modified paint metadata requires a BSP
rebuild. SRF surface indices are bounded by the loaded BSP; malformed/duplicate
indices, truncated records and invalid parent references are rejected. Separate
LIGHT runs and bounce saves preserve material paint without repeated modulation.
`-onlyents` retains the existing BSP geometry's binding; changing paint in the
MAP still requires a full BSP stage.

Use the matching q3mapx compiler for relighting. Older q3map2 binaries ignore
these fields and can overwrite material RGB. BSP transforms/reordering tools do
not currently regenerate the binding; rebuild from source before relighting a
transformed result. Changes to shader modifiers require the BSP stage too.

The binding does **not** retain original source controls, brush strokes or
pre-modifier paint. MAP decompilation warns when it sees authored paint and adds
that limitation to its recovery report; it does not yet restore paint source.
Retain the original MAP. Recovery of compiled channels versus original paint
remains a separate M11 task.

## Verification

Run `ctest --test-dir build/release -R '^patch_paint$' --output-on-failure`, or:

```powershell
python tests/patch_paint.py --compiler build/release/bin/q3mapx.exe --reference path/to/previous/q3mapx.exe --work-dir build/release/tests/patch-paint
```

The generated corpus checks Q3/JA, flat/curved fields, lighting/material modes,
one/four workers, repeated LIGHT/bounce saves, analytic sample colors, collision
flags, density, shader modifiers, shared-edge color discontinuities, malformed
input/output preservation, BSP/SRF mismatches and entity-only relighting.
Optional reference checks compare ordinary legacy BSP/SRF and LIGHT output.
See [recorded evidence](validation/patch-paint.json) for actual platforms and
test counts. Compiler bytes do not establish editor usability or runtime raster
equivalence; no such claim is made by this round.

The optional `tests/nrc_authoring.py` harness additionally exercises the actual
Windows editor model, MAP/XML serializers, selection, undo stack and Qt actions.
It renders its own widget into a QImage without OS capture or injected input.
Editor-saved axial/BP/Valve maps and an alpha-only patch are compiled and lit;
painted output is compared with independent analytic channel values. See
[native paint evidence](validation/radiant-paint.json). Linux native editor,
broader camera/runtime qualification, interactive input testing and packaging
remain open. The optional `--gl` path now adds native camera rendering and
[reference-engine pixel comparisons](PATCH-MATERIAL-PREVIEW.md).

`ctest --test-dir build/release -R '^patch_grid$' --output-on-failure` runs an
independent rational-polynomial oracle for reduction. It covers float/byte
representability, discontinuities, exponent gaps, subnormals and invalid data.
Proofs require ordinary IEEE arithmetic: non-nearest rounding and
denormals-are-zero are rejected, and fast-math builds of this helper are forbidden.
See [grid-edit evidence](validation/patch-grid.json) for the expanded native
matrix, undo checks, compiler parity and Windows/Linux/sanitizer results.

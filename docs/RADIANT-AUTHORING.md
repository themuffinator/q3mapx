# Radiant surface lightmap density

The first M11 implementation adds **per-face and per-patch lightmap spacing** to
q3mapx and a maintained NetRadiant-custom editor patch. In the patched editor,
the Surface Inspector has a density value, Apply/Inherit actions, selection
feedback and an optional cyan grid in the camera renderer. Ordinary maps keep
their existing syntax and compiler behavior.

This is an experimental authoring integration. The Windows editor model,
serializers, widgets and grid geometry are tested; the camera's rendered pixels
are **not yet qualified**. The grid shows requested spacing, not the compiler's
effective material classification or packed atlas. Shader-aware paint previews,
effective inherited-density previews and compiled-atlas inspection remain open
in [M11](PLAN.md#m11--radiant-painting-and-per-surface-lighting-controls).

The companion [patch RGB/alpha painting panel](PATCH-PAINT.md) supports
`q3mapxPatchDef2`, independent channels, undo and a parameter-space RGBA preview.
Open it from the Surface Inspector. Its source/lighting contract, supported
editing operations and remaining preview limits are documented separately.

## Editing and precedence

1. Use the patched NRC with a Quake III MAP game profile. Open the Surface
   Inspector and select individual faces, whole brushes or whole patches.
2. Enter **world units per lightmap texel** and choose **Apply**. Smaller values
   produce finer sampling and can increase lightmap storage and compile cost.
3. Choose **Inherit**, or apply zero, to restore existing compiler/entity/shader
   settings. Mixed selections are identified before applying a common value.
4. Enable the preview to submit grids for explicit overrides on visible faces
   and tessellated patches. Save and compile with the matching q3mapx CLI.

The supported editor surface types are Quake III axial, brush-primitive and
Valve 220 brush faces, plus patchDef2 patches. A patch has one density for the
whole surface. Patch control-point selection is not a density painting tool.
Other game formats and patchDef3 authoring are outside this integration's scope.

| Setting | Result |
| --- | --- |
| `0` | Inherit the existing compiler/entity/shader behavior |
| `1` through `16384` | Absolute requested world units per texel |
| Positive authored value versus entity/shader size or scale | Authored value takes precedence |
| BSP `-minsamplesize` greater than the authored value | Compiler minimum takes precedence |
| Material or compile path requiring vertex lighting | Remains vertex-lit; an override does not enable lightmapping |

The input is a whole decimal number; signs, fractional/scientific spellings,
nonfinite values and values outside the range are rejected in versioned MAPs.
Changing one surface can repack its shared atlas, so unrelated atlas coordinates
are not promised to remain byte-identical.

## Version 1 source contract

The editor owns the setting on the **Face or Patch object**, including its copy
and undo state. The MAP stores it inline with that primitive. It does not use
transient BSP surface indices, external ordering tables or lossy comments.

A brush containing at least one nonzero override is written as:

```text
{
q3mapxBrushDef1 quake
{
    <ordinary Quake face including all three flags> lightmapSampleSize 8
    <next face including all three flags> lightmapSampleSize 0
    ...
}
}
```

This is a grammar illustration, not a complete brush. The projection token is
`quake`, `brushPrimitives` or `valve220`; all brushes in a MAP must use a consistent
projection. Both inner and outer braces are required in the versioned form.
Every face supplies its three legacy integer flags followed by the density clause,
including faces that inherit. Geometric/texture numeric validation still applies.

An overridden patch uses `q3mapxPatchDef1` instead of `patchDef2`, with one
`lightmapSampleSize N` line immediately after the `( width height 0 0 0 )` header
and before the control matrix. Its controls keep the existing `x y z s t` layout.
The new primitive intentionally does not imply authored vertex colors.

Resetting all faces in a brush, or the patch itself, to zero emits the ordinary
legacy primitive again. The compiler accepts explicit zero in a versioned
primitive and tests it against the equivalent legacy BSP/SRF bytes.

**Maps containing overrides require the patched editor and q3mapx.** Older tools
do not understand these primitive names. Keep source copies before moving maps
between editor versions or game formats; removing the density clause alone does
not convert a versioned primitive back to its legacy grammar. Unknown versions
and malformed clauses produce compiler parse errors before replacing existing
build outputs. No automatic lossy compatibility export is supplied.

The native XML serializer carries `q3mapxSampleSize1` on each `plane` or `patch`.
Invalid XML values produce an error diagnostic and inherit; this existing XML API
does not return a whole-document failure. This patch also repairs inherited XML
round-trip defects: ignored derived polygon trees, a misnamed brush-primitive
matrix element, missing Valve 220 bases and missing patch-control separators.
MAP is the primary supported interchange path; broader XML/game-format migration
has not been qualified.

## Compiler propagation and diagnostics

The compiler carries a separate authored value through brush sides, parsed
patches, draw-surface copies and meta triangles. Classification repeatedly honors
that value without reapplying entity scaling. Meta surfaces with different
authored values cannot merge merely because the global minimum clamps them to
the same effective spacing.

The BSP stage's `.srf` file records `authoredSampleSize` for positive overrides,
alongside the existing effective `sampleSize`. Vertex-lit surfaces can therefore
have an authored value and an effective zero. LIGHT uses the ordinary effective
surface data and native allocation paths; there is no new BSP lump or runtime
engine requirement. Keep the matching SRF with its BSP for separate LIGHT runs.
The SRF is a build diagnostic indexed by output surfaces, not the source-data
association mechanism. A BSP alone does not preserve these original authoring
settings for decompilation.

## Preview limits

The cyan lines intersect actual face polygons and native patch tessellation with
a dominant-axis grid. Curved/steep patches, chart projection, material rules,
minimum spacing, light styles, supersampling, padding and atlas packing can all
make final allocation differ. Selection feedback estimates area divided by
spacing squared; it explicitly excludes those additional costs. Inherited
surfaces have no grid, and the editor does not yet resolve the compiler's shader
eligibility or effective inherited values.

The cache compares exact geometry and spacing. Each surface is limited to 262,144
input triangles, 200,000 grid-cell examinations and 40,000 output line vertices;
nonfinite/out-of-world input or an exceeded budget suppresses its grid. The
renderer isolates its draw state and disables selection tint for the overlay.
Native grid/submission tests do not establish GL raster correctness, seam quality
or production performance on large maps. Those remain explicit qualification
tasks before promoting the preview from experimental status.

## Prepare and build the companion editor

The integration is a core patch because NRC's existing module interfaces do not
provide persistent per-face/per-patch fields, undo serialization or the required
render hooks. It targets NRC revision
[`8216133984031afaa9a857b56ea66dd9c3d54b26`](https://github.com/Garux/netradiant-custom/commit/8216133984031afaa9a857b56ea66dd9c3d54b26),
the project's pinned import. It does not modify an installed editor or its profile.

From the q3mapx repository, prepare an independent source copy from a local NRC
Git checkout containing that revision:

```powershell
python integrations/nrc/prepare.py --upstream E:/_SOURCE/_CODE/netradiant-custom-8216133-q3mapx --output build/nrc-authoring
```

The output must be absent or empty inside the project's `build/` or `.agents/tmp/`.
Preparation uses `git archive`, rejects links and escaping paths, verifies the
exact original file hashes, applies the maintained patch and verifies all edited
and added source hashes. Edited text is normalized to LF independently of local
Git settings. Existing output directories are never overwritten. The
[manifest](../integrations/nrc/manifest.json) records provenance and checksums.

NRC uses its own Makefile and **Qt 5 Widgets/SVG**; q3mapx's workbench uses Qt 6.
The tested Windows build uses MSYS2 MINGW64 GCC 15.2.0 with the existing NRC
dependencies. In that shell, from the prepared editor directory:

```sh
make MAKEFILE_CONF=msys2-Makefile.conf DEPENDENCIES_CHECK=off \
  DOWNLOAD_GAMEPACKS=no INSTALL_DLLS=no BUILD=release -j6 \
  binaries-radiant-core binaries-radiant-modules install/q3mapx-authoring-test.exe
```

This builds the editor, format modules and optional native harness. The dependency
probe is disabled in the recorded MSYS2 build; required headers/libraries still
must be available. Keep the MINGW64 DLL directory on PATH. For a normal interactive
editor installation, use NRC's documented resource/gamepack installation workflow
with this source; the isolated test gamepack generated below is only a fixture.
No ready-to-install companion editor package or Linux editor qualification is
claimed by this round.

The paint integration changes `PatchControl` and bumps the patch module API to
version 2. Rebuild every patch-using module/plugin with this header; older modules
cannot use the new layout. Third-party topology tools are not paint-qualified.

## Qualification

Run the compiler's `surface_density` CTest, or include a preceding compiler binary
for exact legacy parity:

```powershell
python tests/surface_density.py --compiler build/release/bin/q3mapx.exe --reference path/to/previous/q3mapx.exe --work-dir build/release/tests/surface-density
python tests/nrc_authoring.py --editor-dir build/nrc-authoring --compiler build/release/bin/q3mapx.exe --work-dir build/release/tests/nrc-authoring
```

The native harness creates a marked disposable portable profile and generated
game assets. It invokes actions directly without mouse/keyboard events and paints
only its own Qt widget into a QImage. It does not open the main editor window,
capture the desktop, run a game or validate camera pixels. Worldspawn graph
loading avoids upstream point-entity labels that allocate GL textures; generated
point/door entities are appended for compiler validation.

Evidence in [the validation record](validation/radiant-density.json) covers the
Windows native editor, Windows/Linux compiler Release and Linux Debug ASan/UBSan:
three projection formats, Quake III/JA, ordinary/meta/patchmeta surfaces, one/four
workers, transformed entities, clamping, vertex-lit materials, old-output safety
and legacy parity. A lit 64-unit face spans eight texels at spacing 8 and two at
spacing 32; measured boundary-inclusive footprints are 81 and 9 texels. This
checks actual packed UVs instead of unused legacy chart-width fields.

Remaining work includes native camera raster tests, larger maps, explicit shader
eligibility, inherited previews, baked-atlas inspection, topology-changing editor
operations, lighting seams, other native profiles, Linux editor delivery, patch
painting and retained authoring metadata for decompilation.

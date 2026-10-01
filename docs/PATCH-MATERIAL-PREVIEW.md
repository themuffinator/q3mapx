# Painted patch material preview

The maintained NRC companion has an experimental **Preview selected patch
material in camera** option in the patch paint panel. It replaces the selected
patch's editor image with supported Quake III material stages. It shows authored
RGB/alpha, including the current uncommitted stroke, on the actual curved surface.
Hide the panel or turn the option off to restore normal editor rendering.

The parameter-space canvas remains the raw control-field view. The camera option
uses neutral white lighting; it does not predict a finished light bake. The panel
states the active contract and explains unsupported materials. Those materials
retain their ordinary editor rendering. **Reload preview material** rereads its
definition and stage images. Native shader refreshes also invalidate the preview.
The dialog scrolls on smaller displays.

**Pixel qualification remains incomplete.** The recorded Intel/Mesa comparison
passes 66 of 68 views under the unchanged limits below. Two curved blended views
have mean channel errors of 0.107253 and 0.102504 against a limit of 0.10; every
channel in those two images differs by at most two 8-bit levels. These are small
color differences, but they still fail the declared gate. Do not use this
experimental preview as a finished-bake or exact game-pixel reference.

## Supported contract

| Part | Behavior |
| --- | --- |
| Source | Explicit shader definition resolved by NRC's game filesystem; eight stages maximum; bounded 4 MiB source and token/nesting limits |
| Images | `map`, `clampMap`, `$whiteimage`, `$lightmap`; exact named TGA/PNG/JPEG through available decoders; extensionless names require a TGA |
| RGB | `identity`, `identityLighting`, `vertex`, `exactVertex` under identity lighting of 1 |
| Alpha | `identity`, `vertex`, `oneMinusVertex`, including Quake III's vertex/identity interaction |
| Blending | `add`, `filter`, `blend`, and legal source/destination combinations of zero, one, source alpha, inverse source alpha, destination color, inverse destination color, source color and inverse source color |
| Coverage/depth | `alphaFunc GT0/LT128/GE128`, `depthFunc lequal/equal`, opaque `depthWrite`, front/back/two-sided culling |
| Lighting | `$lightmap` and vertex lighting are white; material RGB retains authored values |
| Mesh | Direct quadratic samples of geometry, UV and RGBA; at least 16 segments per span, or the authored 32; at most 65,536 vertices |
| Sampling | Bilinear base mip, editor texture gamma; temporary wrap/filter/anisotropy settings are restored |

The camera mesh is cached until its source changes. Model transforms use the
transformed controls; paint operates on independent snapshots. Shader and image
resources are held through the editor's caches. Texture changes between frames
borrow Qt's shared context. Each draw restores GL state, programs, buffers,
texture matrices, client arrays and texture-object parameters. Selection fill is
suppressed for the preview, and Qt's framebuffer coverage alpha is preserved.
Untextured/wire modes use the native patch fallback.

Unknown directives reject the complete material. This includes animation, texture
coordinate generators/modifiers, deformation, fog/sky, compiler color/alpha
modifiers, custom sorting and shader constructs outside the table. Repeated or
malformed directives, duplicate definitions, missing images and excessive data
also fall back explicitly. Destination-alpha factors and blended `depthWrite`
are excluded because framebuffer formats and engine-specific depth behavior
differ. Some harmless shader directives are consequently excluded too.

True-color 24/32-bit TGA and RLE decoding is bounded before allocation and packet
reads. It follows Quake III's legacy bottom-left interpretation even when a TGA
declares another origin, and preserves entirely transparent images. NRC's ordinary
TGA viewer has different compatibility behavior. PNG/JPEG use NRC's decoders and
have not received the same runtime corpus coverage. A decoded stage image is
limited to 4096×4096; source image files are limited to 64 MiB.

## Limits and compiler interaction

This is a selected-patch authoring preview. Surrounding objects still use NRC's
editor materials. Intersecting transparent surfaces and scene draw ordering are
not a game renderer simulation. Baked lightmaps, light styles, dynamic lights,
fog volumes, animation, runtime LOD seams and other native game renderers remain
unqualified. Non-default engine overbright/gamma settings can also change the
displayed result. The tested neutral configuration has engine/map overbright 0,
gamma/intensity 1, base-mip bilinear sampling and no dynamic lights.

The compiler can choose different finite tessellation through geometric/entity
settings. Equality of the continuous control field is not a pixel-equivalence
guarantee for two different meshes. The render corpus uses an identical 16-segment
mesh and verifies geometry, **absolute UVs**, RGBA and triangle connectivity before
and after LIGHT.

Painted compiler surfaces now retain their authored UV origin. Previously, the
inherited texture-bias pass shifted individual meta surfaces by integer repeats.
That is invalid for `clampMap` and can break nonperiodic stage effects. Rebuild the
BSP to obtain this fix. Existing BSP/SRF paint bindings remain valid for their
existing geometry; LIGHT does not reconstruct lost UV origins. Ordinary unpainted
surfaces retain their previous behavior.

## Reproduce validation

Build the companion as described in [Radiant authoring](RADIANT-AUTHORING.md).
Install its own `setup/data/tools/gl` and `bitmaps` resources in the isolated
editor's `install` directory before GL tests. Run:

```powershell
python tests/nrc_authoring.py --editor-dir .agents/tmp/radiant-density/editor --compiler build/release/bin/q3mapx.exe --work-dir build/release/tests/nrc-material --gl
ctest --test-dir build/release -R '^paint_material$' --output-on-failure
```

The optional `--gl` harness uses a hidden Windows Qt context and owned FBOs. It
compares direct draws with NRC's actual camera render cache, exercises the panel
hook without showing a native window, and checks state restoration, resource
refresh, fallback and context borrowing. It does not capture the OS or inject
mouse/keyboard events. The default harness still works without GL.

On Linux/WSL, compare those native captures with an external reference engine:

```sh
python3 tests/renderer/paint_material_render.py \
  --engine path/to/quake3e.x64 --engine-source path/to/Quake3e \
  --assets path/to/Q3A --compiler build/linux-release/bin/q3mapx \
  --native-dir build/release/tests/nrc-material \
  --work-dir .agents/tmp/paint-material/runtime-final
```

Assets are read-only. Generated maps, profiles, independent file copies and logs
stay in the supplied project-local directory. Quake3e runs windowed with SDL
offscreen/software Mesa, input and network disabled, and saves images through its
registered `screenshot` command. Each case loads its final material before the
engine sorts the BSP. Test-only shader-name substitutions leave the verified
compiled mesh intact; runtime shader remapping is not used. The lightless fixture
uses `_keepLights 1` to bypass the separately tracked LIGHT-only MAP first-shader
initialization problem.

Pixel limits are declared in the harness: at most 307 of 307,200 pixels exceed
two channel quanta, and mean channel error at most 0.10. Engine self-repeat must
be byte-identical, and a deliberately different material must visibly differ.
These are cross-driver finite-raster limits, not a lossless rendering claim.
The current optional engine comparison exits with a failure for the two blended
views described above; the gate has not been relaxed or those cases excluded.
All 34 engine repeat captures match exactly, and both compiled meshes match the
native geometry/absolute UV/RGBA/connectivity before and after LIGHT. Native
direct draws and NRC render-cache draws match for all 68 views. See
[recorded evidence](validation/paint-material.json) for actual results.

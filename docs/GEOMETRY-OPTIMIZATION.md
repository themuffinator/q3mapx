# Conservative BSP triangle reduction

Development builds after 0.3.0 provide a post-LIGHT triangle optimizer in the
CLI and workbench. It removes redundant interior vertices from eligible planar
surfaces without changing their vertex records or boundaries. The initial
renderer profile is **Quake3e OpenGL**, with Quake III IBSP46 input. Other engines,
renderers and native formats are not qualified for publication.

The generated ambient fixture drops from 512 to 144 grid triangles across six
surfaces. Engine counters confirm 368 fewer submitted triangles at every tested
view. Vertex count and BSP size are unchanged. These are synthetic triangle-count
results, not a measured frame-time, compile-time or memory improvement.

## CLI and workbench

Analyze a final baked BSP using the matching game's shader assets:

```sh
q3mapx -game quake3 -fs_basepath /path/to/game -threads 4 \
  -optimize-geometry -report geometry.json final.bsp
```

This writes only the report and describes a proposal for the `quake3e-gl` profile.
To publish a separate BSP, select that renderer explicitly:

```sh
q3mapx -game quake3 -fs_basepath /path/to/game -threads 4 \
  -optimize-geometry -renderer quake3e-gl \
  -o final.optimized.bsp -report geometry.json final.bsp
```

Optional controls:

| Option | Effect |
| --- | --- |
| `-exclude-shader name` | Protect the exact shader name; ASCII case and slashes are normalized. Repeat for additional names. |
| `-exclude-surface N` | Protect an original surface ID. Repeat for additional IDs. |
| `-max-work N` | Bound mapping/core analysis work to 1–1,000,000,000 units; default 50,000,000. This is an operation budget, not a time estimate. |
| `-report file.json` | Override the default source basename plus `.geometry.json`. |

In the workbench choose **Analyze geometry · Quake3e GL** or **Optimize geometry ·
Quake3e GL**. Both require advertised compiler/profile support. The queue stages
the input in a new run folder and retains the command, logs, project snapshot and
`geometry.json`. Optimization also writes `<basename>.optimized.bsp`. Reports are
available under **Reports & profiles**. Per-surface/shader exclusions and a custom
work budget currently use the CLI; interactive region review remains planned.

## Eligibility and preservation

The native adapter admits only world-owned planar/triangle surfaces satisfying
all these conditions:

- An explicit, uniquely defined material in the available shader inventory,
  with one or two supported opaque image/lightmap stages. Unknown directives,
  deformations, fog, transparency, alpha tests, environment mapping, turbulent
  coordinates, polygon offsets and other unqualified behavior protect the surface.
- Both the material and stored BSP flags already specify `nomarks` and `nodlight`.
  The optimizer never adds these flags. Marks can depend on per-triangle fragment
  limits; dynamic-light passes produced reproducible finite-raster differences.
- Horizontal positions, constant vertical normals and constant native RGBA.
  Base and lightmap coordinates are exactly affine across the complete surface,
  with consistent winding. A one-ULP discontinuity fails the applicable guard.
- A baked lightmap exists whenever a stage uses `$lightmap`. Submodels, shared
  model ownership, patches, flares and fogged surfaces remain unchanged.

The [reduction core](PLANAR-REDUCTION.md) then requires a simple, consistently
wound interior fan, exact planarity and exact affine interpolants, preserving
coverage, boundary subdivisions and external-edge topology. It makes no geometric
approximation. A successful edit removes two triangles; the greedy ordering does
not promise a globally minimal mesh. Non-affine point-light colors protect the
entire affected native surface in this initial adapter.

Only the selected surfaces' first-index/index-count fields and newly allocated
index slots change. Original vertices, surface IDs, all lump offsets and lengths,
file size, collision, contents, model ownership, VIS, entities and baked data stay
byte-identical. Native compilers can share index subsequences between surfaces:
the allocator retains every slot used by an unchanged surface and places reduced
indices into unreferenced contiguous slots. If the existing lump cannot fit the
chosen allocation, publication fails without enlarging or partially rewriting it.

Shader assets must remain the analyzed ones at runtime. Shader remapping and
renderer extensions are outside this contract. The original id Software renderer's
specialized iterators do not consistently honor `nodlight`; its behavior is not
covered merely because the BSP format matches. The explicit renderer choice is
therefore part of the publication contract.

## Bounds, identity and output guarantees

Input is limited to 512 MiB and 100,000 surfaces. Candidate surfaces are limited
to 65,536 vertices and 131,072 triangles. Shader inventory includes unlisted files
and duplicate VFS occurrences, with limits of 4,096 filenames, 128 occurrences
per file, 4 MiB per file, 64 MiB total and 65,536 unique definitions. The parser
also bounds tokens, token lengths and nesting, rejects NUL anywhere, restricts
tokens to ASCII and limits image names to the renderer's 63-byte capacity.
Malformed structure or unreadable
bounded sources fail the command; unsupported material behavior is reported per
surface. Duplicate definitions remain protected rather than assuming precedence.

The mapping/core budget is distributed by candidate triangle count, deterministically,
with the core's own per-surface cap. Independent surfaces use the job pool; output
ordering is stable across worker counts. Hard work exhaustion abandons the whole
proposal. Inventory, native loading, guards, allocation and serialization also
perform work outside that mapping/core counter.

The report records source/result SHA-256, every shader source hash, renderer and
contract IDs, triangle counts, surface decisions, index allocation, removals and
work/rejection counters. Complete BSP and shader identities are rechecked before
publication. Concurrent edits after that final check are not supported.

The loader validates IBSP46 and its references; `-force` cannot bypass the format
gate. Unknown trailing BSP extensions are rejected. BSP/report destinations must
be distinct from the input and each other, including equivalent paths. Both files
are staged through checked output streams with rollback on reported publication
errors. Multiple filesystem names are not one crash-atomic transaction; see
[output guarantees](OUTPUT-SAFETY.md).

## Renderer evidence and limits

The [validation record](validation/geometry-optimize.json) retains identities,
commands, test results and the failed broader qualification experiment. The first
material policy allowed dynamic lights on horizontal surfaces. Nineteen of 60
comparisons exceeded the predeclared tolerance, with a maximum channel difference
of 2/255 and up to 2,338 changed pixels. Exact repeat captures ruled out capture
instability. Requiring authored `nodlight` narrowed eligibility; the tolerance
was not relaxed.

The restricted fixture passes 60 comparisons: five cameras/distances, four light
configurations, and classic, VBO/per-pixel and vertex-light configurations of
Quake3e OpenGL. The dynamic lights still illuminate other eligible scene materials;
the reduced material disables them by author choice. Twenty repeat controls are
byte-identical. A deliberate 16-unit height error changes 71,308 pixels, confirming
that the visible reduced plane is actually observed. Engine counters report
530 → 162 scene triangles at every view, retaining 376 submitted vertices.

The unchanged acceptance threshold is at most one 8-bit channel step, affecting
at most 0.1% of pixels. Observed maxima are one step and 61 of 307,200 pixels
(0.020%). This is finite-raster qualification on the recorded software Mesa
llvmpipe renderer, not bit-identical pixels or proof for every GPU, camera and
engine version. Geometric/interpolant invariants provide the separate exact
contract. Broader native maps, hardware GPUs, material modes, orientations,
animation, engines and renderer backends remain development work.

The original grid fixture's face winding was corrected to agree with imported
normals before this visible-face test. Its six native partitions now reduce to
144 triangles. The preceding core-only record's 146/496 figures describe its older
fixture, not the current render qualification. The current native adapter leaves
the point-light fixture at 512 triangles because its RGBA varies.

## Reproduce

Build `q3mapx` and `quake3_materials_test` with the project's presets, then run:

```sh
ctest --test-dir build/release -R '^(geometry_optimize|quake3_materials)$' -V
```

The native tests compare every BSP byte outside the allowed fields/slots, protected
index sequences, source preservation, boundary edges, one/four-worker parity,
idempotence and real loader acceptance. Material/native mutations, shared indices,
renderer selection, oversized loose/packed scripts and publication/work-limit
failures have explicit controls. Workbench tests run both workflows through the
real compiler and check unavailable capabilities before staging.

Optional renderer validation requires an external Quake3e source/build and lawful
Quake III assets, not distributed with q3mapx:

```sh
python3 tests/renderer/geometry_render.py \
  --engine /path/to/quake3e.x64 --engine-source /path/to/Quake3e \
  --assets /path/to/Q3A --fixture build/release/tests/geometry-optimize/ambient \
  --work-dir .agents/tmp/geometry-render
```

Run from the project root on Linux/WSL. The harness compiles an original minimal
cgame against the reference engine's compatible GPL headers, draws fixed cameras
and lights, and invokes the engine's registered `screenshot` command. SDL's
offscreen backend, windowed mode, disabled input devices and a private writable
home keep desktop/input and reference assets untouched. Logs, commands, native
TGA render targets, counts and comparison JSON stay under the chosen work directory.
This optional qualification is separate from ordinary asset-independent CTest.

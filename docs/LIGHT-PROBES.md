# Direct lighting probes

`-light -probes` evaluates explicit world-space points using the compiler's CPU
direct-light transport. It reports contributions from retained entity lights,
shader emitters, sun/sky and optional proposed lights separately. This supplies
a reference model for the [light recovery roadmap](RECOVERY-INFERENCE.md).
It does not discover missing lights, export entities or reproduce a complete bake.

```sh
q3mapx -game quake3 -fs_basepath /path/to/game -threads 4 \
  -light -probes request.json -probe-report probes.json example.bsp
```

Select the actual game profile and asset paths. Native IBSP and RBSP readers,
including Qfusion FBSP, are supported; recovery-only readers are rejected.
The default report is `example.light-probes.json`. The report must be a separate
`.json` file; aliases of the BSP or request are rejected. Input hashes are checked
again before publishing the staged report. Reported failures preserve a previous
report through the existing [checked output writer](OUTPUT-SAFETY.md).

The command reads the BSP and current shader/image/model assets. It does not read
the original MAP or SRF, inject command-line entities, replace generated shaders,
repack an atlas or write BSP/MAP/SRF/lightmap files. Original surface extras and
bake settings cannot be recovered by this mode. Use independent files if changing
assets between runs; concurrent source, asset or destination mutation is unsupported.

## Request schema 1

```json
{
  "schema_version": 1,
  "samples": [
    {"surface": 2, "position": [64, 0, 1], "normal": [0, 0, 1], "offset": 0}
  ],
  "lights": [
    {"origin": [0, 0, 128], "intensity": 300, "color": [1, 0.25, 0]},
    {"origin": [0, 0, 224], "intensity": 400, "target": [0, 0, 0], "radius": 64}
  ]
}
```

The surface number must identify a surface in this BSP. Choose coordinates and
normals from inspected geometry or qualified
[baked-lighting observations](LIGHTING-EVIDENCE.md); the example numbers are not
universal. The surface supplies receiver material/shadow behavior. The diagnostic
does not project the point onto that surface or verify that it lies on it.
Positions and normals are world-space, including for inline brush models.
Normals are normalized and `offset` moves the point along that normalized normal.
No hidden luxel nudge, phong interpolation or bump-normal reconstruction occurs.

`samples` is required and contains 1–10,000 entries. Each entry requires `surface`,
`position` and `normal`; `offset` defaults to zero and accepts −16 through 16 map
units. Vector components must be finite, between −1,000,000 and 1,000,000, and
representable as native floats without underflow to zero. Normal magnitude must
be at least 1e-12. Objects reject duplicate and unknown fields, and JSON must be
valid UTF-8 without embedded NUL bytes.

`lights` is optional and contains at most 256 proposals. Each requires `origin`.
The optional properties map to ordinary compiler entity keys:

| Request property | Entity key / interpretation |
| --- | --- |
| `intensity` | `_light`, −1,000,000 through 1,000,000; zero retains the native default of 300 |
| `color` | `_color`, three components in 0–1; native normalization and selected sRGB rules apply |
| `spawnflags` | Native integer light flags, 0–127; interpretation depends on profile/`-q3`/`-wolf` |
| `fade` | `fade`, 0–1,000,000; native zero/default semantics apply |
| `angle_scale` | `_anglescale`, 0–1,000,000 |
| `extra_distance` | `_extradist`, 0–1,000,000 |
| `style` | `style`, integer 0–253 |
| `target` | World-space position of a private, in-memory `info_null` target |
| `radius` | `radius`, 0–1,000,000; native spotlight radius/target-distance convention |
| `sun` | Boolean `_sun`; true requires `target` |

Omitted values use normal compiler defaults. A target turns a point proposal
into a spotlight, or a sun when requested. Private target names avoid surviving
targetnames, and no existing entity links are changed. Proposals are supplied
hypotheses; recreating a stripped light by supplying its known parameters is a
forward-equivalence check, not evidence of inverse localization.

## Evaluation and report

The compiler creates actual point, spot, area, backsplash and sun/sky sources,
then applies its normal envelope construction, source nudges and culling. Each
sample runs through `LightContributionToSample`, including attenuation, visibility,
occlusion, alpha/filter shadows, receiver two-sidedness and negative-light flags.
Patch curve lengths are recomputed from stored controls with the compiler's curve
metric; this does not recover the author's tessellation settings. Inline models
require exactly one surviving entity pose and use the ordinary compiler's entity
origin handling, not animated runtime placement.

Schema 1 includes BSP/request SHA-256 identities, selected profile/settings,
generated/active/culled source counts, per-source native parameters and provenance,
material observations and per-sample responses. A source can identify a surviving
`bsp_entity`, a `proposed_light` array index or its generating `surface`. Surface
emitters include compiler-generated point/backsplash sources. Repeated sky shaders
follow normal compiler source-creation rules; the recorded surface is the source
of that creation, not a claim about the author's placement intent.

Each response contains a source index, pre-encoding `linear_rgb` and the tracer's
`subsampling_signal`. `direct_by_style` sums responses separately for every style
present. It does not impose the native bake's four-style packing limit. Ambient
and minimum light are separate metadata and are not added to these sums.

| Sample status | Meaning |
| --- | --- |
| `sampled` | All active sources were evaluated; omitted source responses are zero under the selected model |
| `outside_usable_cluster` | No usable cluster was found with tolerance 0.125; illumination is unknown |
| `trace_node_limit` | A trace reached its fixed node capacity; all partial responses for the sample were discarded |

An empty array on an unknown sample is not measured darkness. Requested
subsampling is reported but not executed. Gamma, exposure, compensation,
brightness, contrast and saturation are recorded as context; output encoding is
not applied. Bounce, dirt, floodlight, filtering, luxel reconstruction,
supersampling and final clamping are absent. Do not subtract these floating-point
responses directly from stored RGB bytes. Calibration and complete bake effects
remain prerequisites for qualified inverse fitting.

Material records expose available shader text, resolved shader/light images,
default-image use, missing requested normal images and relevant material flags.
They do not hash assets or establish that current files match the original bake.
Missing shader text is unresolved provenance, not proof of an originally implicit
shader. A resolved fallback light image is not a complete missing-asset diagnosis.

## Options and limits

Probe mode accepts these light options, with their normal parser bounds:

- Source scales: `-point`/`-pointscale`, `-spherical`/`-sphericalscale`,
  `-spot`/`-spotscale`, `-area`/`-areascale`, `-sky`/`-skyscale`.
- Scalar settings: `-gamma`, `-exposure`, `-compensate`, `-brightness`, `-contrast`,
  `-saturation`, `-extradist`, `-thresh`, `-lightanglehl`.
  `-backsplash` takes both its fraction and distance arguments.
- Flags: `-wolf`, `-q3`, `-nofastpoint`, `-fast`, `-faster`, `-notrace`,
  `-patchshadows`, `-onesky`; the `-sRGB`, `-sRGBlight`, `-sRGBtex`, `-sRGBcolor`
  flags and their `-no…` forms; `-styles`/`-style` and `-nostyles`/`-nostyle`.

Other light options fail explicitly. Normal global filesystem, game and worker
options remain available. GPU dispatch is not part of this CPU reference mode.

| Resource | Limit |
| --- | --- |
| Request / report | 4 MiB / 64 MiB |
| Native BSP | Less than 2 GiB, plus existing native validation |
| Scene | 100,000 entities, 2,000,000 vertices, 200,000 surfaces |
| Surface preparation estimate | surfaces × (leaves + leaf-surface references) ≤ 50,000,000 |
| Generated sources | 16,384, with conservative preflight source/sky estimates |
| Patch / sky setup | At most eight patch subdivision iterations; sky iterations 2–128 |
| Emitter subdivision | Positive values below one map unit are unsupported |
| Source/sample pairs | Default 5,000,000; `-probe-max-pairs` accepts 1–20,000,000 |
| Retained nonzero responses | 100,000 across all samples |
| Concurrent sample jobs | At most 32, on the persistent worker pool |

Surface ownership must be complete and nonoverlapping. The probe guard checks
native entity origins, colors, intensities and selected count/style/flag fields
before evaluation. Checked integer fields require integer spelling, so values
such as `1e2` or `1.0` cannot disagree with the native integer reader. Conservative
setup estimates may reject a scene that would
ultimately create fewer sources. Pair limits bound evaluations, not ray steps or
elapsed time. Other scene/asset setup still uses existing compiler algorithms.

See [reproduction instructions](DEVELOPMENT.md#direct-lighting-probes) and
[recorded validation](validation/light-probes.json). Qualification covers synthetic
IBSP/RBSP/FBSP fields, real material tracing and output preservation. The fixed
trace-node exhaustion branch and every resource ceiling do not yet have dedicated
fixtures; no full-bake equivalence or source-light discovery claim follows from
these checks.

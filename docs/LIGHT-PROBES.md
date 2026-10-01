# Direct lighting probes

`-light -probes` evaluates explicit world-space points using the compiler's CPU
direct-light transport. It reports contributions from retained entity lights,
shader emitters, sun/sky and optional proposed lights separately. It can also
select geometric internal-lightmap observations and compare an encoded direct
hypothesis with the stored texels. This supplies
a reference model for the [light recovery roadmap](RECOVERY-INFERENCE.md).
Optional [point fitting](POINT-FITTING.md) can now propose missing point lights
under that fixed hypothesis. [Spotlight fitting](SPOT-FITTING.md) adds native cones,
direction/position refinement and target-link proposals. Neither exports entities
or reproduces a complete bake.

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

Choose either `samples` or `baked_lightmaps`, never both. Explicit `samples`
contains 1–10,000 entries. Each entry requires `surface`,
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

## Automatic internal-lightmap comparison

Replace `samples` with `baked_lightmaps` to select texel centers from the existing
geometric lighting-evidence analysis. The same command and optional `lights` array
then evaluate the direct hypothesis at those points:

```json
{
  "schema_version": 1,
  "baked_lightmaps": {
    "stride": 4,
    "normal_offset": 1,
    "max_samples": 10000,
    "max_observations": 200000,
    "max_work": 50000000
  },
  "lights": []
}
```

`normal_offset` is required. It is an explicit sampling assumption in −16 through
16 map units, not an inferred bake nudge. `stride` defaults to four and accepts
1–1024; eligible atlas x/y coordinates must be multiples of it. Optional `surfaces`
is a nonempty list of distinct native surface indices. Omission selects all
surfaces. `max_samples` accepts 1–10,000, `max_observations` 1–200,000, and `max_work`
1–1,000,000,000; the example shows their defaults. The observation/work budgets
cover the underlying extraction, including vertex/grid records and surfaces that
are later excluded by the selection. Surface selection does not bypass those
extraction limits. Budget exhaustion fails without publishing a partial report.

Selection supports indexed triangles and stored biquadratic patches. It excludes
ambiguous, unresolved, boundary and zero-normal mappings, constant-UV regions and
unavailable internal pages. Surviving inline-model origins convert selected
positions to world space. It uses interpolated stored normals and the requested
normal offset; original triangle-edge nudges, axial offsets on oblique surfaces,
phong/bump normals, curve tessellation and source surface extras remain unknown.
Vertex/grid lighting, external lightmaps and deluxe directions are not compared.

For each selected surface/style, the comparison sums direct responses, adds
world ambient and applies world minimum light on slot zero, then uses the current
material's lightmap brightness and the compiler's shared color-encoding function.
The function retains the actual contrast, gamma, exposure, saturation,
compensation, RGB range normalization, sRGB and byte-conversion order. It does not
invert stored bytes. CLI/profile settings and current materials describe the
tested hypothesis; they are not recovered original bake settings. Discarded
per-surface ambient/shadow overrides, bounce, filtering, dirt, floodlight and
supersampling can all leave residuals even when the original direct lights survive.

The optional `baked_comparison` object records sampling, budgets, work, exclusions
and assumptions. An empty `selected_surfaces` array in the report means all
surfaces were considered. Exclusion names identify their units: `surface_*`
counts surfaces, `slot_*` counts slots, `*_triangles`/`constant_regions` count
primitives before representative-record striding, and mapping exclusions count
sampled texel associations.

Each selected sample has a `baked_lightmap` object containing slot/style/page,
texel coordinates, observed RGB, current material brightness, predicted RGB and
signed `residual_bytes` (predicted minus observed). It also retains the hypothesis
before encoding and encoded values before native byte conversion. A trace with
unknown illumination produces `unknown_trace`; a nonfinite or out-of-byte-range
transfer produces `unrepresentable_encoding`. Neither contributes a numeric error.
Subsampling requests remain marked on comparisons that did not execute them.

`comparison_summary` reports MAE, RMSE and maximum absolute component error in byte
units for all compared observations, each style, and the subset without an
observed channel equal to 255. A 255 channel can indicate lost information; lower
values do not prove an invertible transfer. Shared atlas texels may occur in
multiple surface associations, so these equal-observation-weight summaries are
not independent statistical estimates. With no usable observations, status is
`no_usable_observations` and error metrics are null, not zero.

This mode can compare retained lights, a missing-light baseline and supplied
proposals under one fixed hypothesis. It does not generate positions, fit light
parameters, calibrate unknown encoding, prove that a residual needs an entity
light or establish the author's original light arrangement.

Add the optional [`fit_point_lights` request](POINT-FITTING.md) to run bounded
point-position/color/intensity search after the baseline comparison. Its separate
`point_fit` report records conditional proposals, alternatives, withheld validation
and rejection reasons. Existing baseline fields retain their prior interpretation.

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
not applied to the direct response arrays. The optional baked comparison applies
them in its separate hypothesis result. Bounce, dirt, floodlight, filtering, luxel reconstruction,
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

See [reproduction instructions](DEVELOPMENT.md#direct-lighting-probes),
[direct-probe validation](validation/light-probes.json) and
[baked-comparison validation](validation/light-comparison.json). Qualification covers synthetic
IBSP/RBSP/FBSP fields, real material tracing and output preservation. The fixed
trace-node exhaustion branch and every resource ceiling do not yet have dedicated
fixtures; no full-bake equivalence or source-light discovery claim follows from
these checks. Controlled axial-room bakes qualify the encoded comparison across
native formats and several transfer settings. Curved/material scenes retain
small residuals and unknown samples; bounced lighting retains a larger unexplained
component. These distinctions are recorded rather than hidden by a zero-error claim.

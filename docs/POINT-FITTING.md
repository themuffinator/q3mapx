# Conditional point-light recovery

`-light -probes` can now search for a small set of missing point lights from
internal baked lightmaps. Candidate positions come from BSP space, without the
original MAP, SRF or supplied source positions. Retained lights and supplied
proposals stay fixed; current surface emitters, backsplash and sun/sky sources
form the same baseline as [direct lighting probes](LIGHT-PROBES.md).

The result is a conditional lighting explanation. It does not establish the
author's original lights or recover unknown bake settings. The command writes
only its checked JSON report. Separate [spotlight fitting](SPOT-FITTING.md) now
proposes directions, cones and target links. Qualified reports can be
[applied to a recovered MAP](LIGHT-RECOVERY.md) through `-decompile -light-proposals`.
Calibration and workbench review remain open.

## Running a fit

Save a request such as:

```json
{
  "schema_version": 1,
  "baked_lightmaps": {"stride": 2, "normal_offset": 1},
  "fit_point_lights": {
    "grid_spacing": 96,
    "max_lights": 4,
    "max_intensity": 1000,
    "refinement_steps": 6,
    "max_candidates": 1024,
    "max_work": 50000000,
    "min_improvement_rmse": 1,
    "max_rmse": 2
  }
}
```

```sh
q3mapx -game quake3 -fs_basepath /path/to/game -threads 4 \
  -light -probes fit.json -probe-report fit-report.json \
  -q3 -nosRGB -gamma 1 -compensate 1 example.bsp
```

These encoding flags and the one-unit normal offset are explicit assumptions,
not universal settings. Choose the actual profile, current assets and a justified
bake hypothesis. Avoid contradictory global and per-channel color-space flags:
the legacy parser applies option groups in a fixed order. For sRGB lightmaps
with linear textures/entity colors, use `-sRGBlight -nosRGBtex -nosRGBcolor`
without a global `-nosRGB` override. Inspect the report's effective `settings`.

The existing `baked_lightmaps` and `lights` contracts still apply. Point fitting
requires automatic baked observations; explicit `samples` cannot be used as the
fitting target. It fits one requested light style at a time.

| Field | Default | Accepted range / meaning |
| --- | --- | --- |
| `grid_spacing` | 64 | 1–1,000,000 map units; maximum initial cell width |
| `mins`, `maxs` | BSP world-model bounds | Both three-component vectors, −1,000,000 through 1,000,000; each minimum must be less than its maximum |
| `max_candidates` | 1024 | 1–4096 initial grid cells, including rejected solid cells |
| `max_lights` | 4 | 1–16 additional point lights |
| `max_intensity` | 1000 | 0.01–1,000,000 native light intensity per source |
| `refinement_steps` | 6 | 0–10 position scales, beginning at half `grid_spacing` and halving each level |
| `style` | 0 | 0–253; other lightmap styles remain outside the fit |
| `min_improvement_rmse` | 1 | 0.01–64 byte units; required improvement for each greedy addition and for final qualification |
| `max_rmse` | 2 | 0.01–64 byte units; maximum final training and withheld RMSE |
| `max_work` | 50,000,000 | 1–1,000,000,000 charged sample/leaf work units |
| `allow_implicit_materials` | false | Allow absent shader text as an explicit assumption; known missing/default images still prevent fitting |

Unknown/duplicate properties and invalid numeric values fail before report
publication. Grid-limit or work-limit exhaustion also preserves the previous
report. Restrict the search bounds, increase spacing/observation stride or select
fewer surfaces when a full scene exceeds the limits. Observation extraction and
the fixed-source baseline retain their separate existing budgets. Increasing
limits changes cost, not the reliability of the bake assumptions.

## Search and validation

The initial grid divides each search axis into equal cells no wider than the
requested spacing and tests their centers. Opaque/unusable positions are excluded.
Native envelope setup and PVS/material tracing evaluate nonnegative inverse-square
point lights with native angle attenuation and the selected extra distance.
Point colors are normalized, intensity is fitted independently of position, and
the report converts color back to the selected entity color space. Wolf-style
profiles require the corresponding inverse-square spawnflag; arbitrary linear,
dark, jittered or spot families are not searched.

Candidate searches run in the persistent CPU worker pool, up to 32 active jobs.
Each candidate uses bounded channel brackets and local refinement, followed by
greedy source addition. Existing fitted lights are refined with the others fixed
to reduce overlap bias. Every objective evaluation uses native attenuation and
tracing, including intensity-dependent cutoffs and texture filters. This initial
solver does not use an approximate cached ray basis or GPU backend.

Training minimizes squared distance to the encoding's quantization-bin centers;
sRGB's rounded values use the observed integer centers. Qualification uses the
actual byte predictions. Up to eight initial grid alternatives retain their
positions, linear intensities and training objective for inspection; they are
unrefined alternatives, not confidence probabilities. Local search, finite bounds,
greedy source counts and native byte quantization can leave several equivalent
solutions or miss a better solution.

One fifth of deterministic hashed atlas blocks is withheld. Block width is
`max(4, 2 * stride)` texels, and all associations of a given atlas block receive
the same assignment. Withheld colors do not guide source selection, stopping,
intensity/color updates or position refinement. At least 24 training and 12
withheld observations are required. Final qualification requires the specified
improvement and absolute RMSE on both sets. This is a withheld-texel check within
one BSP, not independent-scene validation or proof of unique source placement.

Observed 255 channels, unknown baseline traces and unrepresentable baseline
encoding are excluded. Other sampling exclusions follow the baked-observation
contract. No usable observations produce null metrics. Missing material text
prevents a fit unless implicit materials were explicitly allowed; known missing
requested images and default shader images prevent it even with that override.
Available assets can still differ from the original bake, and complete asset
identity/provenance remains open.

Work is charged conservatively for scheduled sample scoring/tracing and for the
leaf count at each position preparation. It is not an instruction, ray-step or
elapsed-time guarantee. Position refinement has separate fixed iteration bounds;
`max_candidates` limits the initial grid, while `positions_tested` includes later
refinement attempts. A failed budget check publishes no partial report.

## Reading the result

Baseline `sources`, `samples` and `comparison_summary` keep their prior meaning;
fitted lights are not silently added to that baseline. The optional `point_fit`
object contains the search assumptions, exclusions, charged work, training and
withheld scores, alternatives, `best_trial` and native sample-index associations.
`validation_observations` records the split, predicted bytes and any requested
subsampling. The diagnostic does not execute that extra subsampling.

| Status | Meaning |
| --- | --- |
| `conditional_point_proposal` | `accepted` is true; both score gates pass under this fixed hypothesis |
| `baseline_explains_observations` | Training needs no addition and both baseline scores are within the requested absolute limit |
| `baseline_below_search_threshold_but_unqualified` | Training improvement is below the search threshold but the baseline does not meet both absolute limits |
| `validation_rejected` | A training-selected trial failed the final score gates |
| `no_supported_improvement` | Search found no sufficiently improving addition |
| `insufficient_observations` | Too few usable training/withheld records |
| `unresolved_materials` | Material prerequisites are missing |
| `no_usable_candidates` | The requested grid contains no usable candidate positions |
| `unknown_validation_trace` | A final trace is unknown; no numerical trial qualification is supplied |

`best_trial` is a rejected trial unless `accepted` is true. Each trial entry
includes `origin`, native `intensity`, entity-space `color`, `style`, `spawnflags`,
`extra_distance`, and diagnostic `linear_intensity_rgb`. To evaluate a trial as
explicit probe `lights`, copy only the request properties and omit that diagnostic
field. No entity or target link is created by this command.

Retained sources and materials are fixed, so incorrect assets or encoding, bounce,
dirt, floodlight, filtering, original luxel nudges/normals, surface overrides and
other omitted effects can leave residual error. An accepted fit can still explain
such errors with the wrong source arrangement. Review the spatial alternatives,
assumptions and residuals, and recompile a separate recovered map before adopting
any proposed lights. Wider real-map calibration and review tools remain required.

## Qualification

The [native test matrix](DEVELOPMENT.md#point-light-fitting) removes original
lights and poisons the original MAP/SRF before inference. It covers off-grid and
overlapping colored lights, retained sources, opaque occlusion, multiple native
formats/styles, linear/gamma/sRGB/coupled encoding and entity sRGB. Full decompile,
BSP, VIS and LIGHT rebuilds compare observations by world position/normal/style,
independently of atlas packing. Altered withheld blocks test validation isolation;
sun-only/emitter-only and bounced/incorrect-encoding controls check rejection.
These synthetic controls do not establish general author-light recovery accuracy.

See [the recorded evidence](validation/point-fitting.json). This round also
corrects two earlier fixture limitations: Qfusion plain-room tests now load their
shader directory correctly, and explicitly checked per-channel switches ensure
the sRGB-labelled cases actually enable sRGB. The earlier
`validation/light-comparison.json` sRGB-labelled cases ran linear encoding because
of their global override; use the corrected matrix recorded with this round for
sRGB qualification.

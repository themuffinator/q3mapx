# Conditional spotlight recovery

`-light -probes` accepts an optional `fit_spot_lights` request alongside
`baked_lightmaps`. It searches for missing native spotlights with unknown
positions, colors, intensities, directions and cone widths. Retained lights,
supplied proposals, current surface emitters and sun/sky remain fixed. It writes
only a checked JSON report; inferred lights and target links are proposals for
review, not recovered original author metadata or automatic MAP edits.

The [point-fitting contract](POINT-FITTING.md) supplies the common observation,
material, encoding, style, budget and output-preservation rules. Choose exactly
one of `fit_point_lights` and `fit_spot_lights` per request. Mixed families can be
evaluated with explicit fixed `lights`; automatic point-versus-spot model
selection remains open.

```json
{
  "schema_version": 1,
  "baked_lightmaps": {"stride": 1, "normal_offset": 1},
  "fit_spot_lights": {
    "grid_spacing": 96,
    "max_lights": 2,
    "max_intensity": 1000,
    "min_half_angle_degrees": 5,
    "max_half_angle_degrees": 75,
    "max_work": 200000000
  }
}
```

Run this through the same CLI as a point fit, with an explicit game, filesystem
and justified encoding hypothesis. The report exposes effective settings. This
does not calibrate unknown bake settings, reconstruct indirect lighting or prove
that unexplained texels came from an entity light.

## Search

The native spotlight is an inverse-square source with angle attenuation. Its
outer cone slope is `(radius + 16) / distance_to_target`; its boundary softens
over 32 map units. A radius alone is not an angular width. Fitting uses the actual
compiler attenuation, visibility and material tracing, including these rules,
the selected spot scale and the explicit extra-distance setting. Custom falloff,
dark/jittered lights and original luxel/subsampling reconstruction are not fitted.

Training residuals supply a centroid, channel and surface centroids, and up to
four spatial clusters, with at most eight distinct direction seeds. Eligible
surviving target markers provide additional directions. Each usable BSP-grid position tests five
initial cone widths. Bounded CPU jobs fit initial colors/intensities; multiple
starting positions undergo coordinate refinement and joint damped least squares
with finite differences through native transport. The continuous objective uses
native encoding before final sRGB rounding. Qualification always uses actual
native bytes. No approximate cached ray basis or GPU backend is used.

The solver is local and bounded. Coarse grids, incomplete direction seeds, finite
iterations, intensity ceilings and an explicit family choice can miss a better
explanation. A spotlight on one plane may have ambiguous height, direction and
intensity even when a rebuild is similar. Compare source-parameter errors and
lighting errors separately on known-source cases; a good footprint does not
establish the original light position.

## Options and bounds

Common fields retain the point-fitting ranges. Spotlight defaults differ:
`grid_spacing` is 96, `max_lights` is 2, `refinement_steps` is 7, and `max_work` is
200,000,000. These additional fields apply only to `fit_spot_lights`:

| Field | Default | Meaning |
| --- | --- | --- |
| `min_half_angle_degrees` | 5 | Minimum outer cone half-angle, 1–85 degrees |
| `max_half_angle_degrees` | 75 | Maximum half-angle, 1–85 degrees and greater than the minimum |
| `max_spot_candidates` | 8192 | At most 1–32768 position/direction/cone combinations per greedy addition, including rejected combinations |
| `refine_candidates` | 4 | Refine 1–8 distinct initial positions |
| `use_retained_targets` | true | Use eligible surviving markers as direction constraints and link candidates |

Each refinement scale permits four coordinate passes. Joint polishing permits
at most ten iterations per requested refinement scale, with at most nine
parameters per source. Additional sources can be provisionally added before
joint refinement. With multiple sources, the solver also reconsiders each
source's origin grid while holding the others fixed, then jointly polishes the
result. This helps correct an earlier source that approximated overlapping lobes
from the wrong position. The complete addition must meet the improvement gate or
the previous solution is restored. Candidate preparation, sample evaluation,
residual clustering and dense normal-equation work are charged to `max_work`.
This is not a ray-step or elapsed-time guarantee. Exceeding a candidate/work limit preserves the previous
report instead of publishing a partial fit.

The recorded two-source overlap fixture uses a one-billion-unit work budget;
its 700-million-unit request exhausted the bound safely. Complex fits can exceed
the defaults. Restrict plausible search bounds and inspect the validation record
when choosing a budget; a larger allowance does not guarantee a useful fit.

At most 128 eligible static markers can participate. `use_retained_targets: false`
explicitly disables that prior, including its candidate-count cost. It does not
permit generated names to collide with surviving names or references.

## Qualification and report

The optional result is `spot_fit`; ordinary baseline `sources`, `samples` and
`comparison_summary` remain unchanged. Deterministically hashed atlas blocks are
withheld from search, residual seed generation, target selection and stopping.
The common minimum of 24 training and 12 withheld observations still applies.

In addition, positive baseline residuals larger than `min_improvement_rmse`
identify illuminated support. At least 12 training and six withheld observations
must support the fit. Both ordinary and illuminated-only sets must pass the
requested improvement and absolute RMSE gates. This prevents a large dark
background from hiding poor reconstruction of a small lit footprint. Withheld
support affects only final qualification, not source selection or refinement.
`validation_observations` exposes the support mask and exact predicted bytes;
`training` and `withheld` include `illuminated_baseline` and `illuminated_trial`.

`conditional_spot_proposal` is accepted under the stated hypothesis.
`insufficient_spot_support` identifies inadequate positive support; the other
material, observation, unknown-trace and rejection statuses follow point fitting.
`best_trial` must not be treated as qualified when `accepted` is false. Withheld
blocks belong to one BSP, not independent scenes or a calibrated probability
model. Trace-requested subsampling is reported but not executed.

## Target links

Unique, positioned `info_null` and `target_position` entities without a model or
outgoing `target`/`target2` link are eligible static markers. Duplicate names,
other classes, missing origins and excluded markers are reported separately.
Their current BSP origins are an explicit static-pose assumption. Existing
entities, names and gameplay links are never changed by the diagnostic.

Marker-constrained candidates use their actual target distance and a positive
native radius. A free fit also checks nearby marker directions within five
degrees, jointly refitting position, intensity and cone. It prefers an existing
marker when its training objective is within 0.01 squared byte units of the free
fit. Final withheld gates still apply. This preference reduces added entities;
it is not proof of the author's original target or unique target identity.

Each trial contains `direction`, `radius_by_distance`, `half_angle_degrees`,
explicit probe-compatible `target` coordinates and `radius`. Its `target_link`
contains the proposed MAP `targetname` and source BSP entity index, or a dedicated
new `info_null` marker with a unique `_q3mapx_inferred_target_…` name. Generated
names reserve surviving targetnames and generated-name tokens in all entity
values, including dangling references, so a new marker cannot silently resolve
one of those references.

To evaluate a trial as explicit probe `lights`, copy `origin`, `intensity`,
`color`, `style`, `spawnflags`, `extra_distance`, `target` and `radius`. Omit the
diagnostic fields. For a separately recovered MAP, a light's string `target`
must name `target_link.targetname`; create the proposed marker only when its
`bsp_entity` is null. Recompile and compare corresponding world samples before
adopting the proposal. Automatic entity export and GUI accept/reject review
remain open.

## Validation

`tests/spot_fitting.py` strips original lights and optionally targets, poisons
the original MAP/SRF, and compares hidden source labels with fitted parameters.
Successful proposals are attached to independently decompiled maps and fully
rebuilt through BSP/VIS/LIGHT. Native observations are matched by world
position/normal/style, independently of atlas packing. The corpus includes
encoding/styles, overlapping colors, retained sources, target identity and
collision controls, withheld-data changes and preserved-output failures.
See [the validation record](validation/spot-fitting.json) for measured errors,
platform coverage and qualifications.

These controls qualify only their declared native hypotheses. Complete asset
provenance, unknown bake calibration, more complex shadow/indirect effects and
general real-map recovery accuracy remain open.

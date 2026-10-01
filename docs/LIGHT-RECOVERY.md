# Applying light proposals to a recovered MAP

`-decompile -light-proposals <report.json>` applies a qualified
[point](POINT-FITTING.md) or [spotlight](SPOT-FITTING.md) fitting report to a
separate recovered MAP. It exports the fitted lights, necessary new target
markers and every caller-supplied fixed light on which the fit depended. Surviving
entities retain the ordinary decompiler's behavior; this option never changes
their target links. Inference remains explicit and conditional on the recorded
lighting hypothesis.

```sh
q3mapx -game quake3 -fs_basepath /path/to/game -threads 4 \
  -light -probes fit-request.json -probe-report fit-report.json \
  -q3 -nosRGB -gamma 1 -compensate 1 example.bsp

q3mapx -game quake3 -fs_basepath /path/to/game \
  -decompile -brush-order rebuild -light-proposals fit-report.json \
  -o recovered.map example.bsp
```

Run the second command after the fit succeeds. The example's encoding flags are
an explicit hypothesis; they are not defaults to assume for an unknown BSP.
Inspect the fit's settings and residuals, then rebuild the recovered MAP with
compatible assets and lighting settings before adopting the result.

The option supports classic, brush-primitive and Valve 220 MAPs, fast/full
recovery, detail/group inference and legacy `-convert -format map…`. It always
writes the normal recovery report, including when used through `-convert`.
One fitting report can be selected per export. It requires compiled BSP input
and a writable native profile, and excludes `-wtf` material replacement.
The `-games` catalog advertises `recovery_light_proposals` for supported profiles.
The existing CLI remains available without the option and retains its output.

## Qualification checks

The importer requires a schema 1 point or spotlight fitting report with the
expected family/status and `accepted: true`. It verifies the exact input BSP's
SHA-256, selected game and native BSP header. It rechecks the following stored
evidence before generating entities:

- Surface, lightmap slot, style, page and texel associations, and observed RGB
  against the actual internal BSP lightmap bytes. Saturated observations are
  excluded by the fitting contract and cannot qualify an import.
- Unique validation sample indices and the deterministic atlas-block split.
- Baseline/trial sample counts, MAE, RMSE and maximum error, recomputed from the
  report's recorded predictions and observed texels.
- Training/withheld improvement and absolute-error gates. Spotlight imports also
  recompute the illuminated-support mask and its separate counts and score gates.
- Fitted color/energy/intensity, native style/flags and spotlight direction,
  target position, radius and cone consistency. Values must fit supported native
  ranges and MAP representation.

This verifies record consistency and input identity. It does not rerun native
transport, authenticate a report's author or prove that edited predictions came
from the compiler. A coherent fabricated report is not a lighting certificate.
Original bake settings, asset identities, omitted effects and source uniqueness
retain the limitations of the fitting stage. Keep the original report and
recompile/compare actual output rather than treating `accepted` as proof of the
author's original lights.

## Fixed sources and target links

Fitting reports now include `fixed_proposals`, preserving the original validated
`lights` request. These dependencies are exported even if native envelope setup
culled them. Active-source records and proposal entity indices must agree with
the dependency list. Reports predating this addition remain usable when no
caller-supplied light proposals were present; otherwise regenerate the report.
Surviving BSP lights, material emitters and sky/sun definitions are not duplicated.

Fitted entities use explicit native intensity, color, style, spawnflags and
extra distance. Fixed entities retain their requested attenuation, color, style,
spot/sun and radius parameters; an omitted extra distance is made explicit from
the fitting hypothesis. Entity colors remain in the recorded entity color space.
Global settings such as point/spot scale, gamma, sRGB, compensation, tracing and
falloff are recorded for the rebuild; MAP entities cannot encode every global
compiler option, and export does not alter the project's build configuration.

A spotlight may reuse only its report's uniquely named, explicitly positioned
`info_null` or `target_position` with the same BSP entity index, name and origin,
without model ownership or outgoing `target`/`target2` links. Other existing
entities are not retargeted. New inferred markers retain their checked
`_q3mapx_inferred_target_N` names. Fixed proposal targets receive unique
`_q3mapx_fixed_target_N` names. Name reservation includes generated-name tokens in
all surviving entity values, protecting dangling and custom references as well
as ordinary targetnames. Colliding inferred names fail instead of silently
resolving an unrelated link.

MAP comments label each added entity's role and proposal index. The recovery
report's optional `light_recovery` object records input/report hashes, family,
source/generated entity counts, fitted/fixed-light counts, reused targets,
required lighting settings, qualification scores and emitted MAP entity indices
with their exact keys. Generated groups and lights have distinct, correctly
offset indices. Original authorship, target identity, fresh transport validation
and rebuilt-lighting validation remain explicitly unproven.

## Resource and publication rules

Reports are regular UTF-8 JSON files up to 64 MiB. Parsing is iterative and rejects
duplicate properties, embedded NULs, excessive nesting/structure and malformed
types. Limits include depth 64, two million values, 10,000 samples/validation
records, 16 fitted lights and 256 fixed light proposals. Strings, native numbers
and target names are checked before MAP serialization. A report can add at most
544 lights/markers. No arbitrary entity class, model, key or output path is
executed from its contents.

BSP/report inputs are checked before loading and again before publication.
Output paths cannot alias either input, including through hard links or symbolic
links. Existing MAP/report output protections apply: both outputs are completed
before publication, failed validation/writes preserve previous files, and the
report is rolled back if MAP replacement fails. Concurrent input/output writers
and process/power-loss durability remain outside the guarantee.

## Validation and remaining work

`tests/light_recovery.py` constructs native bakes, strips original light entities,
poisons MAP/SRF files, fits sources, exports them through the actual decompiler
and performs independent BSP/VIS/LIGHT rebuilds. It checks corresponding world
samples rather than assuming atlas packing survived. Coverage includes all MAP
formats, fast/full recovery, retained and new targets, supplied point/spot and
culled dependencies, native styles, sRGB entity colors, Wolf attenuation flags,
detail/groups and preserved-output failures for inconsistent or malformed input.

The CLI provides the application step. Interactive proposal review, native fitting
controls in the workbench, joint point/spot model selection, unknown bake
calibration and general real-map reconstruction accuracy remain open.

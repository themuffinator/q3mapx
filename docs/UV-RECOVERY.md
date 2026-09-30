# Multi-triangle texture recovery

Development builds after 0.3.0 use multiple rendered triangles to check and, when
needed, improve each recovered brush face's affine texture mapping. This operates
in the existing native decompiler and workbench decompile jobs. It does not
recover missing source UVs from lighting or infer the author's exact editor values.

```sh
q3mapx -decompile -uv-policy consensus -o recovered.map example.bsp
q3mapx -decompile -uv-policy triangle -o comparison.map example.bsp
q3mapx -convert -format map_bp -uv-policy consensus -report recovery.json example.bsp
```

`consensus` is the default. `triangle` retains the preceding largest-overlap
triangle path, integer-offset wrapping and decimal serialization for compatibility
and comparison. An explicit policy requires MAP
export without `-fast`; fast export continues using its existing fallback axes.
The workbench inherits the default without a new required setting. Spatial UV
overlays and per-face manual corrections remain planned.

## Selection and fitting

The existing material/bounds hierarchy supplies overlapping rendered triangles.
The consensus collector additionally requires each vertex to lie within 0.01
units of the brush plane and the geometric normal components to agree within
0.0001. The broader legacy search remains available as a fallback, including its
autoclip tolerance. Joint fitting uses only the selected material and model.
Early-format material inference retains its separate selection policy.

Consensus mode canonicalizes cyclic vertex order and equal-area ties within a
material. It first checks whether the selected triangle transform, including its
binary32 intermediate representation, already explains all retained samples.
When it does, that transform is kept. Otherwise, a centered, scaled, weighted
least-squares fit uses all three vertices of every retained triangle, weighted
by clipped overlap area. Centering avoids cancellation in translated geometry;
canonical sample sorting makes the regression independent of input traversal.
Model-local positions are translated by the surviving entity origin before fitting.

Every input sample must agree with the candidate in both texture coordinates.
The component allowance, in texture repeats, is:

```text
min(1/4096, max(1e-6, 8 * float_epsilon * max(1, abs(st))))
```

This is a bounded recovery tolerance for binary32 data and arithmetic, not a
probability or proof of author intent. A large weighted average cannot conceal
a small incompatible chart: every sample participates in the maximum-error gate.
Ill-conditioned geometry, unsupported constant axes, non-finite/out-of-range
results and insufficient storage precision retain a triangle/fallback transform.
The candidate is checked after binary32 coefficient conversion too; classic MAP
conversion also checks coefficient range after converting repeats to pixels.

A detected UV seam or conflicting mapping is reported and is never averaged into
the exported transform. Choosing one triangle does not reconstruct every chart
on a face that contains multiple mappings. Compiler-generated integer UV biases
can also produce conflicts; they are not silently unwrapped because texture
addressing semantics may make an integer offset significant.

## Output precision and texture addressing

Consensus output keeps the recovered whole texture offsets. Reducing an offset
modulo one texture repeat can change `clampmap` and other non-repeating material
behavior; equality modulo a tile is not sufficient evidence of fidelity.
Brush-primitive matrices and classic MAP parameters use nine significant decimal
digits, enough to round-trip their stored binary32 values. Derived Valve 220
bases, shifts and scales use 17 significant digits before the MAP reader's own
binary32 conversion. Scientific notation retains small gradients/scales that
fixed eight-decimal output could turn into zero. The affine fitting and existing
classic-shear approximation policies remain unchanged.

Patch control positions and UVs use nine significant digits as well, including
fast export. This preserves the stored binary32 values after the existing origin
addition; it does not recover precision already lost during compilation or that
addition. The explicit triangle compatibility path retains the older six-decimal
patch representation. Brush plane-point serialization is a separate mechanism
and is not changed by texture precision.

A Valve pixel shift may overflow MAP storage even though the native offset in
texture repeats is finite. Consensus export validates its derived parameters,
reports the affected brush/plane, and uses a finite fallback in that case. The
optional schema-1 `uv_output` object names the serialization `policy`, whether
full recovery `preserves_integer_offsets`, and the number of
`unrepresentable_valve_faces`. These describe export policy and exceptions, not
a universal round-trip or author-intent guarantee. UV fit decisions/residuals
remain distinct from later output-representation failures.

Absolute reconstruction still depends on compatible assets and compiler settings.
The compiler normally biases rendered UVs by integer repeats unless the shader
uses `q3map_globaltexture`. Decompilation cannot restore a bias that is absent from
the BSP, and a later compile can apply new biases. The absolute-coordinate tests
use a clamped shader with `q3map_globaltexture` so that this behavior cannot hide
an export error. No shader semantics are silently changed by recovery.

## Bounds and reports

Each face retains at most 4,096 strict coplanar candidates, or 12,288 vertex
samples. Exceeding the candidate cap discards the partial fit and completes the
ordinary largest-triangle search. In early formats the collection cap includes
all candidate materials before the selected material is filtered. No truncated
sample set is presented as complete evidence. This bounds the additional fitting
storage/work; it does not impose a new bound on the pre-existing spatial search.

Version 1 recovery reports add the optional `uv_recovery` object:

- `policy`, `enabled` and `author_mapping_proven` identify the selected path;
  author mapping is never marked proven.
- `counts` separates `consistent` joint fits, `triangle_consistent` retained
  transforms, insufficient triangles, conflicting mappings, sample limits,
  ill conditioning, constant axes, invalid numbers and representation limits.
- `faces` records multi-triangle decisions with normalized BSP brush/plane IDs,
  contributor counts and supporting surface IDs. These are compiled-record
  associations, not original MAP face identifiers.
- `rms_error`, `max_error` and `max_tolerance_ratio` describe the evaluated affine
  field. Errors are in repeats and are `null` when no residual was evaluated.
  They do not measure subsequent decimal serialization or rebuilt BSP error.
- At most 10,000 face records and 64 unique surface IDs per record are retained.
  `omitted_records` and `omitted_surfaces` expose truncation. A candidate-limit
  record has no retained surface list because its partial collection was discarded.

Reports retain the existing schema version and earlier counts. Existing consumers
can ignore this optional extension. MAP/report publication keeps the existing
[checked-output guarantees](OUTPUT-SAFETY.md).

## Validation and limits

`uv_fit` checks the numeric core's success/failure invariants. `uv_fit_oracle`
compares it with exact rational normal equations solved by Gaussian elimination,
independently of the production centered covariance implementation. Its 240 cases
include translated data, native float quantization, sample permutations and
uniform weight scaling.

`uv_recovery` exercises 60 native recovery cases, with two rebuilds per case,
across IBSP/RBSP, world and translated brush-model geometry, and all three MAP
writers. Controlled native render records model small visible islands with
quantized UVs, reordered triangles, conflicting charts, a nearby unrelated plane
and the candidate cap. The generating affine field is used only for fixture
creation/evaluation. Tests compare rebuilt UVs using one uniform integer bias per
channel, check exact brush planes/materials/contents, verify seam/limit fallback,
and compare one/four-worker and triangle-order output. Optional `--reference`
requires the explicit triangle policy's MAP bytes to match the preceding compiler.

These fixtures do not establish recovery of arbitrary authored MAPs. Existing
round-trip tests additionally cover oblique brushes, patches, grouping, lighting,
native recovery-only formats and output failures. See [test instructions](DEVELOPMENT.md#multi-triangle-uv-recovery)
and [recorded fitting evidence](validation/uv-consensus.json).

`uv_output` separately checks absolute coordinates through 24 native rebuilds in
IBSP/RBSP and all three MAP formats, with both world and translated brush geometry.
Cases cover signed/multi-tile offsets, large offsets, small gradients and small
scales. Patch control positions/UVs must match exactly; brush coordinates are
checked with an explicit binary32 allowance without any integer-bias correction.
Eight additional controls cover diagnosed Valve range fallbacks and patch
precision during fast export. Optional preceding-binary comparisons reproduce
the losses and require unchanged triangle-policy MAP bytes. See
[output evidence](validation/uv-output.json).

Classic MAP syntax cannot express arbitrary shear, and constant UV axes still
use the existing fallback. Extreme transforms, serialized brush geometry,
split-face reconstruction and broader renderer/editor round trips need separate
qualification. This feature improves supported affine recovery and exposes
conflicts; it does not complete all decompilation or light-inference work.

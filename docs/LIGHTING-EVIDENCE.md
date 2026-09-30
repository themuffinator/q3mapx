# Baked lighting observations

Development builds after 0.3.0 can export bounded lighting observations alongside
the [BSP evidence report](BSP-EVIDENCE.md):

```sh
q3mapx -game quake3 -threads 4 -bsp-evidence -lighting map.bsp
q3mapx -game ja -bsp-evidence -lighting -lighting-stride 4 -report light.json map.bsp
```

This implements the first data-extraction step of [light recovery](RECOVERY-INFERENCE.md#light-entity-and-spotlight-inference).
It does **not** fit or export inferred light entities or targets. The command
needs no game assets and does not modify the BSP. It preserves encoded bytes,
styles and native record associations so future fitting can distinguish observed
data from assumptions about the bake.

## Contract

The optional `baked_lighting` object has its own `schema_version: 1`. The outer
BSP evidence schema remains version 1; without `-lighting`, this object is absent.
The native IBSP and RBSP reader adapters are supported, including Qfusion's FBSP
variant and profile-sized 128/512 atlases. Other readers return
`unsupported_native_adapter` with no lighting observations. This qualification
is for stored data extraction, not every game's light transport or renderer.

| Field | Meaning |
| --- | --- |
| `status` | `observations_only` or an unsupported adapter |
| `encoding` | Stored bytes with an unknown bake transfer function |
| `sampling` | Requested stride, limits, combined observation count and worker bound |
| `atlas` | Stored byte count, profile page size, complete/reference page counts and lump status |
| `surfaces` | Native normalized surface/shader IDs, model ownership and coordinate space, lightmap slots and vertex/control samples |
| `grid` | Normalized record count, layout status and per-style stored grid observations |
| `limitations` | Missing evidence and numerical assumptions; `light_inference_performed` is always false |

Styles 254/255 are unused. Other styles remain separate, even if they reference
the same page or share vertices. Surface shader names come from the BSP table;
shader scripts and images are not loaded. Source SHA-256 and before/after
stability checks are inherited from the evidence command.

## Surface observations

Each active lightmap slot retains its stored page and style. Indexed planar and
triangle-soup surfaces invert their lightmap UVs at atlas **texel centers**.
The resulting records contain texel coordinates, encoded RGB, interpolated
position/normalized normal, first surface-local triangle, triangle hit count,
boundary status and a `has_255_channel` endpoint observation. A 255 channel alone
does not prove clipping or saturation.

Shared triangle edges can hit a texel more than once. Records merge only within
one surface/slot. Agreeing hits keep the first mapping; differing positions or
normals set `ambiguous_mapping` and make both position and normal null. A zero
interpolated normal is also null. Separate surfaces and models never merge.
The report follows surface, slot and atlas row/column order, independently of
worker scheduling. It does not promise identical triangle IDs after reordering
or re-triangulating the native BSP.

Texel positions are geometric associations, not reconstructed original bake
rays. The BSP does not retain the compiler's luxel nudges, supersampling support,
bump normals, padding classification, filtering or dilation history. Boundary
samples deserve particular care. Unknown shader behavior can also change how
an atlas page is displayed; the report follows stored surface references only.
It does not guess external images or interpret unreferenced pages as either
lighting or deluxe directions.

Slot statuses distinguish unused styles, negative/no internal page references,
invalid/unavailable pages, pending patch parameterization, unsupported surface
types, absent indexed triangles and analyzed triangle geometry. Atlas byte counts
that are not a multiple of the selected profile's page size disable all atlas
sampling. Vertex/grid evidence remains available. Degenerate geometric triangles
and degenerate or ill-conditioned UV inverses have separate counters.

Constant UV charts have no unique inverse and currently produce no atlas
positions, even when their stored texel is lit. Patches export stored control
points as `bezier_control`; their control mesh is not substituted for the curved
surface. Actual Bezier lightmap inversion remains open.

Vertex observations retain the native global vertex ID, slot/style, XYZ, stored
normal and exact RGBA bytes. They include surface-range vertices even when an
index list does not use them. Colors can contain authored paint, shader modifiers
and lighting; they are not labelled pure irradiance. This also avoids confusing
future [Radiant paint metadata](PLAN.md#m11--radiant-painting-and-per-surface-lighting-controls)
with recoverable original colors.

Model ownership uses bounded prefix counts over native surface ranges. World
model samples use world coordinates; other models remain explicitly local and
untransformed. Unowned or overlapping ranges have no asserted model. Entity
origins, shared model instances and runtime poses are not guessed.

## Lightgrid observations

Grid records retain style-separated ambient/directed RGB and both direction
bytes. Raven's native loader already expands its dictionary/index array; record
IDs refer to that normalized, expanded sequence. Direction bytes are preserved,
not assigned a guessed engine decoding or original source-light direction.

For a valid worldspawn and world model, layout uses the stored positive finite
`gridsize`, or the conventional `(64, 64, 128)` default. The proposed origin is
`pitch * ceil(world_min / pitch)`, and each dimension extends through
`pitch * floor(world_max / pitch)`. Positions are emitted only if the full
dimension product equals the normalized record count, with X varying fastest.
Malformed or extra numeric tokens, invalid pitch, absent bounds and mismatched
counts remain explicit statuses with null sample positions. Grid bytes remain
available when positions are unknown. Empty grids are `absent`.

`conventional_layout_count_matches` is a consistency check, not proof of the
original compiler's layout. Zero values may be dark or unpopulated. Relocated
sampling rays cannot be recovered from the regular-grid address. No guessed
pitch is fitted to an incompatible count.

## Bounds and numerical behavior

`-lighting-stride N` accepts 1–1024, default 1. Atlas X and Y must each be a
multiple of N; surface-local vertex indices and normalized grid record indices
use the same interval starting at zero. This is deterministic sampling, not an
adaptive or statistically unbiased selection. The option requires `-lighting`.

`-lighting-max-samples N` accepts 1–200,000, default 50,000. It counts one
surface/slot/texel association, one surface/vertex/style association or one
grid-record/style association. Ambiguous observations count too. Exceeding this
bound fails instead of silently publishing a partial subset. Increase the stride
or limit explicitly for larger inputs. Source ceilings are 200,000 surfaces,
2,000,000 vertices and 2,000,000 normalized grid records in addition to the base
evidence limits. Up to 32 workers use the persistent pool, separate per-surface
results and shared atomic work/sample budgets. Dense per-worker lookup storage
is bounded by the profile's page area and reused across styles.

The command shares `-max-work` (default 50 million, maximum 100 million) with
other requested evidence passes. Lighting charges source/observation work,
per-slot atlas lookup initialization, triangles and candidate texel tests.
Increasing the stride reduces samples and candidate tests; it does not remove
the fixed per-slot lookup charge. Sorting and JSON serialization are additionally
bounded by sample counts and the common 64 MiB output ceiling. Work units are
algorithmic guards, not CPU instructions or an elapsed-time guarantee.

UV inversion uses double precision, rejects determinants at or below
`1e-12 * max_edge_component²`, and permits barycentric weights down to `-1e-9`.
Atlas bounds use a `1e-8` pixel expansion before testing. Duplicate mappings
agree within `1e-4 + 1e-9 * max(abs(coordinate))` per position component and `1e-5`
per unit-normal component. These tolerances expose approximate evidence rather
than claiming exact topology. Coordinates are clamped before integer conversion;
extreme finite UVs cannot overflow atlas addressing.

Analysis failures and the output ceiling preserve any existing report using
the evidence command's staged writer. This includes failures inside worker jobs.
Source records and other outputs remain unchanged.

## Next recovery stages

Encoding calibration, patch/constant-chart observations, external/deluxe data,
surface/sky/bounce explanations, candidate light generation, point/spot fitting,
target matching and held-out rebuild validation remain pending. No GPU speedup
or light localization accuracy is claimed by this extraction stage. See the
[test instructions](DEVELOPMENT.md#baked-lighting-observations) and
[validation record](validation/lighting-evidence.json).

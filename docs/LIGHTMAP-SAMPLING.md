# Inherited lightmap spacing

Surface classification now resolves lightmap spacing once and preserves it through
later triangulation, copies and meta merging. Previously, a shader requesting
32 world units per texel combined with an entity scale of 2 could end up at 32
after first resolving to 64. The result depended on subsequent compiler passes;
ordinary brush emission also reclassifies surfaces. The corrected result is 64.

## Precedence and bounds

For a lightmapped surface, the compiler resolves these settings in order:

1. A positive per-face or per-patch `lightmapSampleSize` is absolute spacing.
   It bypasses entity and shader size/scale settings.
2. Otherwise, a positive shader `q3map_lightmapSampleSize` overrides the entity
   `_lightmapsamplesize` / `_samplesize` / `_ss` base. Without either, use the BSP
   `-samplesize` value, whose default is 16.
3. Apply a positive entity `lightmapscale` / `_lightmapscale` / `_ls` once to
   the inherited base. An absent or nonpositive scale leaves that base unchanged.
4. Truncate the positive result to whole world units and enforce the effective
   interval from `max(1, -minsamplesize)` through 16,384. A minimum above 16,384
   saturates at 16,384. Scaling is bounded before integer conversion, including
   products of finite values that overflow floating-point or integer range.

Vertex-lit materials and paths remain vertex-lit with effective spacing zero.
The source parsers' existing entity/shader numeric syntax is unchanged; this is
not a claim that all legacy parser paths are now strict.

The compiler distinguishes pending inherited values from resolved spacing.
Draw-surface copies retain that state. Meta triangles already contain resolved
positive values, so their rebuilt surfaces do not apply the shader base again.
New triangular decals with an unresolved zero still receive their first
classification after merging, including the `-maxarea` path. Existing
merge eligibility compares effective spacing and authored overrides; adjacent
regions with different values remain distinct. Shader clones are created before
initial classification and resolve their own material settings. Later same-shader
fur/skybox copies retain the resolved value.

## Compatibility and diagnostics

Affected MAPs intentionally produce different sampling and may repack their
lightmap atlases. Rebuild BSP/SRF and rerun LIGHT together. This fix does not
preserve the erroneous inherited density as a compatibility mode. Ordinary
no-scale and unit-scale controls retain their preceding native BSP lumps
(excluding command-line provenance) and SRF bytes. Explicit authored overrides
retain their precedence.

The `.srf` file reports effective `sampleSize` and any positive
`authoredSampleSize`. LIGHT may still increase actual sampling to fit atlas limits;
the SRF is not a measurement of final atlas allocation. The Radiant density
overlay still shows authored requests and does not yet predict all inherited
settings or final atlas costs. See [density authoring](RADIANT-AUTHORING.md).

## Validation and remaining issue

`surface_sampling` checks ten numeric/precedence settings across Q3 and JA,
ordinary/meta/patchmeta/maxarea paths and one/four workers: 160 native builds.
It covers brush subdivision, curved patch tessellation, material clones/back sides,
transformed brush entities, forced-meta imported models, adjacent func_groups,
vertex-lit materials, newly projected decals and authored overrides. Group checks
use source-space ownership to prevent merges across differing densities, not just
a set of reported values. Selected complete VIS/LIGHT runs verify actual packed UV spans:
a 64-unit square spans one texel at the corrected spacing of 64.

Optional preceding-compiler comparisons check unchanged controls and reproduce
the lost scale. `surface_density` retains its projection/authoring, clamping,
lightmap and malformed-source coverage with the corrected inherited expectation.

```powershell
ctest --test-dir build/release -R '^(surface_sampling|surface_density)$' --output-on-failure -j 2
python tests/surface_sampling.py --compiler build/release/bin/q3mapx.exe --reference path/to/previous/q3mapx.exe --work-dir build/release/tests/surface-sampling
```

The new fixture also exposed an independent existing issue: `misc_model` with
spawnflag 4 can fail LIGHT with a zero projection axis unless global `-meta` is
enabled. Both preceding and repaired density binaries reproduce it; removing
only that model lets both complete LIGHT. The failures preserve prior BSP bytes.
Ordinary mode is covered through BSP/SRF in this matrix; the twelve atlas checks use meta,
patchmeta and maxarea. Repairing that model path is tracked separately in the plan.
No engine-rendering, seam-quality or editor-preview qualification is claimed.

See [recorded evidence](validation/surface-sampling.json).

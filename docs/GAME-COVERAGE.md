# Game and map-format coverage

The continuing coverage work starts from q3mapx 0.2.0 (`f3235ac`) and the
[fnTech3 reference](https://github.com/themuffinator/fnTech3/tree/a1251ede2c382190b18c154b45357f6979d8171c)
at `a1251ede2c382190b18c154b45357f6979d8171c`. The sibling checkout is read-only
reference material for this work. It is not a q3mapx dependency.

## Initial audit

At the start of this work q3mapx had nineteen compiler profiles, while the
workbench offered only fourteen in its dropdown. The compiler now publishes
all profiles and their supported workflows through `-games` (JSON schema 1).
The workbench queries the selected compiler asynchronously, displays its actual
profiles and format information, and checks the chosen workflow before staging
input. `-help -game` remains available for a readable list.

Profile names and aliases are case-insensitive. Useful aliases include `q3`,
`ql`, `rtcw-sp`, `rtcw-mp`, `wolfet`, `stvef-sp`, `stvef-mp`, `jk2-sp`,
`jk2-mp`, `jka-sp` and `jka-mp`. Unknown names fail instead of silently selecting
Quake III. An alias selects the existing canonical profile; it does not promise
additional format or gameplay support.

All nineteen writable profiles pass generated BSP/VIS/LIGHT, MAP recovery,
recompilation and CPU minimap pipelines on Windows release, Linux release and
Linux ASan/UBSan. [Windows records](validation/game-profiles-win-x64.json) identify
the compiler and each native signature. These are small distributable geometry
fixtures, not retail-game runtime tests.

| fnTech3 title or family | Map contract | Initial q3mapx profile/status |
| --- | --- | --- |
| Quake III Arena | IBSP 46, 17 lumps | `quake3` |
| Quake Live | IBSP 47 with advertisement extension | `quakelive` |
| Return to Castle Wolfenstein | IBSP 47, 17 lumps | `wolf` |
| Wolfenstein: Enemy Territory | IBSP 47, 17 lumps | `et` |
| Star Trek Voyager: Elite Force | IBSP 46 | `ef` |
| Jedi Outcast and Jedi Academy | RBSP 1, 18 lumps | `jk2`, `ja` |
| Heavy Metal: F.A.K.K.2 | FAKK 12, checksum and 20 lumps | Initially missing; `fakk2` recovery added |
| American McGee's Alice | FAKK 42, checksum and 20 lumps | Initially missing; `alice` recovery added |
| Medal of Honor: Allied Assault | 2015 19, 28 lumps, terrain/static-model extensions | Missing |
| Quake III IHV Test | IBSP 43 | Missing |
| Public Q3Test releases | IBSP 44 and 45, different record layouts | Missing |

This table records the format boundary, not complete game compatibility. Shared
ident/version pairs do not establish a game's shader flags, paths or behavior.
Both Jedi titles use RBSP 1 in the inspected reference; no IBSP override is
appropriate. Other inherited profiles include Qfusion's FBSP 1, whose lighting
uses the Raven family of records, and several IBSP-based games.

The asset-independent [BSP inspector](BSP-INSPECTION.md) now recognizes all layouts
in the table, including the missing-reader families. It reports bounded directory
validation separately from geometry and native reader capability. An audit of
the pinned MOHAA header corrected the initial table: its signature is `2015`,
version 19, rather than `FAKK`.

The inherited Raven lightgrid serialization searched every dictionary entry for an
approximate match, in insertion order. It also writes index 65,535 when a full
65,535-entry dictionary cannot represent another point. That index is out of
range. The optimization must retain the earliest matching entry and the existing
per-channel tolerance of four, including the inherited circular direction comparison,
while making exhaustion a controlled error before replacing the destination.

The packing task is now implemented and validated: dictionary references and
payload order match the inherited scan, capacity errors preserve existing files,
and measured native-map rewrite times are recorded in [performance](PERFORMANCE.md).
Reading other retail Jedi maps also exposed non-finite coordinates in unused
lightmap slots. Validation now checks every surface that could use a coordinate
before normalizing an unused non-finite pair to zero. This includes vertex-lit
patches' primary slots. Active coordinates, including vertices shared with an
active surface, remain strict. Zero-geometry flares with fog 0 in a map with
no fog lump are normalized to no fog; geometry-bearing fog references remain
strict. Both repairs print diagnostics and appear in recovery JSON.

All 61 map entries across the installed Jedi Academy archives (including four
patched versions) and all 41 Jedi Outcast entries pass native BSP validation.
`t3_stamp` and `yavin_temple` also pass actual MAP recovery with installed assets
read-only. [Recorded hashes and results](validation/native-raven-win-x64.json)
contain no retail map bytes. These checks establish parsing/recovery coverage,
not gameplay or lossless reconstruction of the original editor source.

The used-slot rule follows Raven's negative lightmap numbers and style sentinel
contract, checked against
[OpenJK's reader](https://github.com/JACoders/OpenJK/blob/1a6a643427aa347553e9073dac5570b33337c4d9/codemp/rd-vanilla/tr_bsp.cpp)
and [style handling](https://github.com/JACoders/OpenJK/blob/1a6a643427aa347553e9073dac5570b33337c4d9/codemp/rd-vanilla/tr_shader.cpp).
No implementation text was copied. Generated regression fixtures cover the same
records and shared-vertex boundary without depending on retail files.

## Evidence and implementation rules

### Alice and F.A.K.K.2 recovery

`alice` and `fakk2` are native recovery profiles, bringing the catalog to 21
profiles while retaining 19 native writers. They load their actual FAKK 42/12
checksum-bearing directories, 76-byte shaders and 108-byte surfaces. The reader
converts record prefixes directly from the input buffer; it does not construct
an intermediate IBSP image. Geometry, entities, patch controls and texture UVs
feed the existing recovery and minimap workflows.

MAP reports retain native shader flags, shader subdivision values and the
per-surface subdivision array. Omitted native entity lighting, light visibility
and light definitions are named with byte counts. Stored lightmaps/lightgrid are
not reconstructed as source lights. MAP export always produces its report for
these profiles, including the traditional `-convert -format map*` spelling.
Native BSP compilation, rewriting and game-to-game BSP conversion fail before
stage outputs are created; GUI catalog capabilities enforce the same boundary.

Ritual FTX textures are now read with checked dimensions/payload sizes and their
RGBA channels preserved. This supplies real texture dimensions during recovery.
Unknown native material directives can still produce warnings; raw shader flags
are retained in the report, and this is not a native shader compiler. Standard
MAP output cannot reproduce every game-specific material/collision contract.

All 36 Alice and 30 F.A.K.K.2 archive maps pass native geometry validation. The
selected `centipede1` and `towncenter_good` maps recover 1,149/2,768 brushes and
44/35 patches, with no skipped brushes and no missing-texture warnings after FTX
support. Hidden faces still use reported fallback UVs. The
[native evidence](validation/native-fakk-win-x64.json) contains hashes, counts and
loss summaries. Generated tests verify native strides, extended metadata,
malformed inputs, unavailable-writer protection, minimaps and a texture-sensitive
MAP/Quake-III recompilation comparison on Windows, Linux and ASan/UBSan.

Layout and FTX observations come from the pinned fnTech3
[BSP header](https://github.com/themuffinator/fnTech3/blob/a1251ede2c382190b18c154b45357f6979d8171c/code/qcommon/bsp_fakk.h)
and [image contract](https://github.com/themuffinator/fnTech3/blob/a1251ede2c382190b18c154b45357f6979d8171c/code/renderercommon/tr_image_ftx.c).
Ritual flag bit meanings were cross-checked against its v1.02 SDK's
`utils/common/surfaceflags.h` (observation only, under the non-free
[Ritual SDK](https://github.com/Sporesirius/fakk2) terms). All new implementations
are independently written; no SDK or translator implementation text is copied.

### Ongoing coverage requirements

- Test native records and malformed ranges, not renamed Quake III files.
- Separate inspection, recovery, conversion and native compilation capabilities.
- Retain explicit profile selection when the file header cannot distinguish games.
- Validate the existing IBSP/RBSP/FBSP pipelines as well as new readers.
- Synthetic fixtures are distributable; retail maps stay in the designated local
  test area or are read from their existing installations, never committed.
- Recovery reports identify omitted lighting, terrain, models or other extensions;
  a successful geometry import must not imply a lossless native rewrite.

fnTech3's relevant first-party format headers declare GPL-2.0-or-later, compatible
with q3mapx's GPL-3.0-or-later distribution. The initial audit uses layout facts
and documented observations, not copied implementation text. Any later code
incorporation must retain its notices and identify its exact source. The Ritual
SDK and retail binaries are observation-only references; no proprietary text or
assets are incorporated.

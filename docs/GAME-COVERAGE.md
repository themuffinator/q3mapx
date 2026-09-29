# Game and map-format coverage

The continuing coverage work starts from q3mapx 0.2.0 (`f3235ac`) and the
[fnTech3 reference](https://github.com/themuffinator/fnTech3/tree/a1251ede2c382190b18c154b45357f6979d8171c)
at `a1251ede2c382190b18c154b45357f6979d8171c`. The sibling checkout is read-only
reference material for this work. It is not a q3mapx dependency.

## Initial audit

q3mapx currently has nineteen compiler profiles. The workbench offers only
fourteen in its dropdown, although an arbitrary profile can be typed. `-help
-game` lists the compiler profiles; there is no structured capability catalog.

| fnTech3 title or family | Map contract | Initial q3mapx profile/status |
| --- | --- | --- |
| Quake III Arena | IBSP 46, 17 lumps | `quake3` |
| Quake Live | IBSP 47 with advertisement extension | `quakelive` |
| Return to Castle Wolfenstein | IBSP 47, 17 lumps | `wolf` |
| Wolfenstein: Enemy Territory | IBSP 47, 17 lumps | `et` |
| Star Trek Voyager: Elite Force | IBSP 46 | `ef` |
| Jedi Outcast and Jedi Academy | RBSP 1, 18 lumps | `jk2`, `ja` |
| Heavy Metal: F.A.K.K.2 | FAKK 12, checksum and 20 lumps | Missing |
| American McGee's Alice | FAKK 42, checksum and 20 lumps | Missing |
| Medal of Honor: Allied Assault | FAKK 19, 28 lumps, terrain/static-model extensions | Missing |
| Quake III IHV Test | IBSP 43 | Missing |
| Public Q3Test releases | IBSP 44 and 45, different record layouts | Missing |

This table records the format boundary, not complete game compatibility. Shared
ident/version pairs do not establish a game's shader flags, paths or behavior.
Both Jedi titles use RBSP 1 in the inspected reference; no IBSP override is
appropriate. Other inherited profiles include Qfusion's FBSP 1, whose lighting
uses the Raven family of records, and several IBSP-based games.

Raven lightgrid serialization currently searches every dictionary entry for an
approximate match, in insertion order. It also writes index 65,535 when a full
65,535-entry dictionary cannot represent another point. That index is out of
range. The optimization must retain the earliest matching entry and the existing
per-channel tolerance of four, including the inherited circular direction comparison,
while making exhaustion a controlled error before replacing the destination.

The packing task is now implemented and validated: dictionary references and
payload order match the inherited scan, capacity errors preserve existing files,
and measured native-map rewrite times are recorded in [performance](PERFORMANCE.md).
Reading other retail Jedi maps also exposed non-finite coordinates in unused
lightmap slots; that separate validation compatibility issue remains to be fixed.

## Evidence and implementation rules

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

# Retained patch sources

New BSP builds retain painted `q3mapxPatchDef2` source controls in a versioned BSP
trailer. It restores patches that have become triangle-only geometry as well as
native collision patches. Use `-no-patch-source` during BSP compilation to omit
this authoring data. Ordinary patches do not create an archive.

```sh
q3mapx -game quake3 map.map
q3mapx -game quake3 -decompile -patch-recovery source -o recovered.map map.bsp
```

The source policy requires a valid archive and geometry binding. It fails before
publishing a MAP/report if these are absent or stale. Default recovery remains
unchanged. All three MAP brush projections support source recovery. Adjacent MAP
and SRF files are not consulted for source controls. LIGHT still requires the
matching SRF for its existing paint-preservation checks.

## What is retained

Each painted primitive retains its pre-tessellation, pre-material-modifier
XYZ/ST/RGBA controls, source shader name, RGB mode, subdivision choice and explicit
lightmap-density override. Positions are captured in model space after source
entity placement. Entity and primitive numbers identify the capture context;
they do not reconstruct discarded `func_group` entities. Associations follow
splits and meta merging without constraining merge decisions. Source recovery
emits each archived primitive once and suppresses its surviving native patch
representation, avoiding duplicate collision/render patches.

Brush strokes, editor selection, original groups, inherited compile parameters,
material assets and discarded modifier volumes are not retained. Rebuilding the
same appearance still depends on that context. The archive is compiler-retained
source evidence, not proof of author identity. It cannot restore original paint
from older BSPs that lack the archive; compiled-channel extraction is documented
in [patch color recovery](PATCH-COLOR-RECOVERY.md).

## Integrity and compatibility

The archive is appended outside native BSP lumps. It does not change the native
header, vertex representation or merge decisions. Engines read their normal lump
ranges; whole-file checksums and download sizes change. Third-party rewriting
tools may strip the trailer. Keep original MAP files as the authoritative source.

SHA-256 protects payload integrity. A separate canonical geometry digest binds
model ranges, surface topology, positions, UVs and immutable painted channels.
Lighting allocation, generated shader names, entity text and lighting-mode baked
RGB are excluded. BSP, VIS, LIGHT, repeated/bounced LIGHT and `-onlyents` retain
the source archive. Entity-only compilation does not replace retained paint with
new source edits. A geometry-changing rewrite drops a stale archive with a
warning; exact recovery rejects mismatched geometry. A checksum is not an
authentication signature, and external changes outside the bound fields are not
certified by it.

## Version 1 encoding

All words are unsigned little-endian 32-bit values. Strings are a word length
followed by bytes, without a terminator. Float fields preserve IEEE float32 bits.
The payload contains version `1`, game-profile string, 64-character lowercase
geometry digest, and record count. Each record has eight words (model, source
entity, source primitive, width, height, paint mode, subdivisions, density), a
shader string, row-major controls, and an output-surface count/list. A control
contains five float words (XYZ/ST) and a packed little-endian RGBA word. The
footer contains payload byte length, 64 ASCII SHA-256 characters, and the 16-byte
magic `Q3MAPX_PATCH_V1` including its terminating NUL.

Limits are 32 MiB of payload, 10,000 primitives, 1,000,000 controls and 2,000,000
surface associations, plus the normal painted-grid limits. Readers validate
native lump separation, lengths, game/version, digest syntax, finite controls,
settings, names and sorted surface references before use. A recognized malformed
archive is an error even during ordinary loading. Unrecognized trailing data is
not interpreted as a source archive.

The recovery JSON adds `patch_recovery`, identifying its retained-source basis,
restored count and verified geometry binding. It explicitly does not claim author
authentication or restoration of original compile context. Output uses the
existing staged MAP/report publication and rollback mechanism.

## Verification

```sh
ctest --test-dir build/release -R '^(patch_source|patch_color_recovery|patch_paint|decompile_recovery|recovery_outputs|uv_recovery|bsp_validation)$' --output-on-failure -j 2
python tests/patch_source.py --compiler build/release/bin/q3mapx --reference path/to/prior/q3mapx --work-dir build/release/tests/patch-source
```

Use `.exe` on Windows. Q3/JA fixtures cover both paint modes, solid/native and
nonsolid/triangle-only output, modifier replay, translated models, five compile
stages and three MAP projections. Source primitives and rebuilt samples must
match independently retained inputs. Archive opt-out and optional prior binaries
check native-lump parity. Checksummed malformed records and stale geometry must
preserve existing MAP/report bytes. See [recorded evidence](validation/patch-source.json).

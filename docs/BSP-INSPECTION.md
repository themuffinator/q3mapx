# BSP inspection

Identify a map and examine its directory without game assets or a guessed profile:

```sh
q3mapx -inspect map.bsp
q3mapx -inspect -json map.bsp
q3mapx -inspect -json -game ja map.bsp
```

The inspector reads at most 256 bytes, opens the input read-only, and never loads
textures or initializes the game filesystem. It reports the signature, version,
file size, candidate profiles, named lump ranges and fixed-record counts. For
checksum-bearing formats it reports the stored value; it does not verify that
checksum. Variable or opaque lumps have a null record count.

Recognized layouts are IBSP 43, 44, 45, 46 and 47, Quake Live's extended directory,
RBSP 1, FBSP 1, FAKK 12 and 42, and Medal of Honor's `2015` 19. Recognition is
independent of native reader availability; the profile catalog describes actual
recovery/compilation capabilities.

JSON uses schema version 1. `inspection_scope` is
`signature_and_lump_directory`, and `geometry_validated` is always false. A zero
exit status and `valid: true` mean at least one matching directory passed checks
for truncation, signed/overflowing ranges, header/payload overlap, overlapping
nonempty lumps and fixed-record divisibility. Cross-lump indices, entities,
finite geometry, lighting and game behavior require the actual native loader.
Even an empty directory can be structurally valid without containing a world.

Several games share IBSP 46, IBSP 47 or RBSP 1. `profile_candidates` and
`ambiguous_game` expose that ambiguity; the inspector does not select a game from
a map name or signature. Without `-game`, IBSP 47 reports both 17-lump and 18-lump
interpretations, including errors for either invalid interpretation. An empty
advertisement extension cannot prove Quake Live identity. Explicit profile aliases
are accepted and resolved to the canonical ID; mismatched profiles fail.

Malformed or unrecognized files return status 1, with structured errors under
`-json`. Nonprintable signature bytes are shown as `?` and retained exactly in
`ident_hex`. Unknown versions never fall through to an assumed record layout.
Unaligned payload offsets are accepted. Files larger than 2 GiB can be inspected,
but remain outside the compiler's native load/write limit.

Generated fixtures cover every recognized directory and malformed boundaries on
Windows and Linux ASan/UBSan. Read-only probes also inspect 120 entries in the
installed Alice, F.A.K.K.2 and Allied Assault archives; the
[evidence](validation/native-inspection-win-x64.json) contains hashes and results,
not proprietary map data. This is directory evidence, not recovery or gameplay
evidence. Use `tests/native_maps.py --inspect` for the optional archive probe.

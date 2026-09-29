# Upstream provenance and credits

## Initial source

- Project: [NetRadiant-custom](https://github.com/Garux/netradiant-custom).
- Retrieved: 2026-09-29.
- Branch: `master` (remote HEAD verified with `git ls-remote`).
- Revision: [`8216133984031afaa9a857b56ea66dd9c3d54b26`](https://github.com/Garux/netradiant-custom/commit/8216133984031afaa9a857b56ea66dd9c3d54b26).
- Commit date: 2026-09-01.
- Commit subject: `Merge pull request #311 from Spike29/spelling`.
- Local read-only reference: `E:\_SOURCE\_CODE\netradiant-custom-8216133-q3mapx`.

The initial q3mapx repository had no license and contained only `.gitattributes`.
Before import, the upstream `LICENSE` and compiler file headers were inspected.
The compiler is GPL-2.0-or-later; bundled libraries have individual notices.
q3mapx adopts compatible licensing and retains those notices and license texts.
The import contains 209 files (3,550,398 bytes): `tools/quake3/common`,
`tools/quake3/q3map2`, upstream q3map2 regression fixtures, the transitive local
header dependencies and image/network support sources, and the upstream license
texts and contributor list. The Radiant editor, game packs, and bundled Assimp
implementation are excluded; Assimp is a system build dependency.

[The import manifest](upstream-manifest.json) records the SHA-256 of each original
file before q3mapx changes. This makes source provenance independently verifiable.
Imported RapidJSON (MIT), Crunch (zlib/public domain), tiny_webp (MIT), DDS support
(BSD), and ETC support (MIT) retain their file-level notices. These permissive
licenses are compatible with the compiler's GPL-2.0-or-later distribution.

## Credits

Thanks to [id Software](https://github.com/id-Software/Quake-III-Arena), the
GtkRadiant and NetRadiant teams, ydnar and the q3map2 contributors, and
[Garux and NetRadiant-custom contributors](https://github.com/Garux/netradiant-custom/graphs/contributors).
The upstream `CONTRIBUTORS` file will accompany the import. Third-party component
licenses remain authoritative for their respective files.

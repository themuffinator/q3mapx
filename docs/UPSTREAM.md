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
q3mapx retains those notices and license texts and distributes the combined work
under GPL-3.0-or-later, exercising the compiler's later-version permission.
The import contains 209 files (3,550,398 bytes): `tools/quake3/common`,
`tools/quake3/q3map2`, upstream q3map2 regression fixtures, the transitive local
header dependencies and image/network support sources, and the upstream license
texts and contributor list. The Radiant editor, game packs, and bundled Assimp
implementation are excluded; Assimp is a system build dependency.

[The import manifest](upstream-manifest.json) records the SHA-256 of each original
file before q3mapx changes. This makes source provenance independently verifiable.
Imported RapidJSON (MIT), Crunch (zlib), tiny_webp (BSD-3-Clause), DDS support
(BSD-3-Clause), and ETC support (Apache-2.0) retain their file-level notices.
The file-level audit corrected the initial documentation's mistaken MIT labels
for WebP and ETC. Apache-2.0 is compatible with GPLv3, but not GPLv2; see the
[Apache Software Foundation's compatibility guidance](https://www.apache.org/licenses/GPL-compatibility).
The compiler's GPL-2.0-or-later permission permits the combined GPLv3 distribution.
`COPYING` contains GPLv3; `GPL`, `LGPL`, and `LICENSE` preserve the upstream texts.
`licenses/` includes Apache-2.0 and RapidJSON notices. The latter was copied from
the same NRC revision's `libs/assimp/contrib/rapidjson/license.txt`; its JSON_checker
section describes an upstream component that q3mapx does not import.

## Credits

Thanks to [id Software](https://github.com/id-Software/Quake-III-Arena), the
GtkRadiant and NetRadiant teams, ydnar and the q3map2 contributors, and
[Garux and NetRadiant-custom contributors](https://github.com/Garux/netradiant-custom/graphs/contributors).
The upstream `CONTRIBUTORS` file will accompany the import. Third-party component
licenses remain authoritative for their respective files.

The optional GPU backend uses three unmodified Apache-2.0 headers from
[Khronos OpenCL-Headers, revision e6060189](https://github.com/KhronosGroup/OpenCL-Headers/tree/e6060189f4ebe8b52d885c37af71b9a50c272154).
The license was checked before incorporation and is compatible with the combined
GPL-3.0-or-later project. Headers and the full license are in
`libs/thirdparty/opencl/`. GPU kernels, the dynamic loader, and spatial sampling
code are original q3mapx code. GPU drivers are supplied by the operating system or
hardware vendor and are not redistributed by this repository.

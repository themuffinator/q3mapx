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

The optional [Radiant authoring integration](RADIANT-AUTHORING.md) maintains a
patch against the same pinned [NRC editor](https://github.com/Garux/netradiant-custom/tree/8216133984031afaa9a857b56ea66dd9c3d54b26).
The modified editor/model/serializer/Makefile headers permit GPL-2.0-or-later;
that later-version permission is compatible with q3mapx's GPL-3.0-or-later work.
Original copyright and license notices are preserved. The patch and original
q3mapx overlays are distributed under GPL-3.0-or-later. The
[integration manifest](../integrations/nrc/manifest.json) records exact upstream
and patched hashes. Preparation creates an independent source archive copy and
does not modify the reference checkout. NRC's Qt 5 and other dependencies retain
their own license obligations; this round does not distribute their binaries or
third-party gamepacks.

The optional geometry renderer harness uses
[Quake3e](https://github.com/ec-/Quake3e) and its id Software ancestry as an external
reference. Its GPL-2.0-or-later header notices and GPL license were checked before
compiling the original `tests/renderer/geometry_cgame.c` fixture against that ABI;
the later-version permission is compatible with this GPL-3.0-or-later project.
No engine implementation or game assets are incorporated or redistributed.
The validation record identifies the local source files and executable by hashes,
since the available reference is a source snapshot without Git metadata.

Additional format-layout observations are credited to the
[fnTech3 headers](https://github.com/themuffinator/fnTech3/tree/a1251ede2c382190b18c154b45357f6979d8171c/code/qcommon),
with the exact revision and evidence boundaries recorded in
[game coverage](GAME-COVERAGE.md). The directory inspector uses independently
written parsing and layout facts; no translator implementation was incorporated.

Thanks to [id Software](https://github.com/id-Software/Quake-III-Arena), the
GtkRadiant and NetRadiant teams, ydnar and the q3map2 contributors, and
[Garux and NetRadiant-custom contributors](https://github.com/Garux/netradiant-custom/graphs/contributors).
The upstream `CONTRIBUTORS` file accompanies the import. Third-party component
licenses remain authoritative for their respective files.

The optional GPU backend uses three unmodified Apache-2.0 headers from
[Khronos OpenCL-Headers, revision e6060189](https://github.com/KhronosGroup/OpenCL-Headers/tree/e6060189f4ebe8b52d885c37af71b9a50c272154).
The license was checked before incorporation and is compatible with the combined
GPL-3.0-or-later project. Headers and the full license are in
`libs/thirdparty/opencl/`. GPU kernels, the dynamic loader, and spatial sampling
code are original q3mapx code. GPU drivers are supplied by the operating system or
hardware vendor and are not redistributed by this repository.

The original q3mapx workbench uses the [Qt 6 Core, Gui and Widgets framework](https://doc.qt.io/qt-6/),
under its compatible open-source license terms; see [Qt licensing](https://doc.qt.io/qt-6/licensing.html).
No Qt example code was copied. Runtime packages must retain Qt and its component
notices and provide the applicable source/relinking information.

Portable builds use unmodified shared libraries from [MSYS2](https://www.msys2.org/):
Assimp, GLib, libxml2, Qt, PNG/JPEG/zlib support and their runtime dependencies.
The packaging manifest identifies the exact components actually shipped and links
their original projects and versioned source packages. License texts are copied
from package-owned files, including ICU's separate `share/icu` notice. The runtime
audit includes GPL/LGPL later-version permissions and GCC runtime exceptions,
permissive BSD/MIT/zlib/Unicode terms, the FreeType license option, bzip2, libpng,
libjpeg-turbo and CC0 notices. These shared-library terms are compatible with the
combined GPLv3 application when their distribution conditions are retained.
Qt's package-level metadata includes tools/documentation licenses beyond the
Core/Gui/Widgets libraries used by this application; all supplied notices remain
available rather than being reduced to a single package label.

This software is based in part on the work of the Independent JPEG Group.
See [release packaging](RELEASE.md) for the exact source, dependency-source
archives, replacement-library support and license layout.

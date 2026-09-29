# Task log

## 2026-09-29 — Plan and upstream audit

Completed the project plan, architecture, development policy, decompilation design,
and attribution documentation. Verified remote NRC HEAD as
`8216133984031afaa9a857b56ea66dd9c3d54b26` and retrieved that reference source.
Inspected the upstream license and compiler build definitions.

Validation: empty initial repository verified; upstream revision independently
queried and cloned; local build toolchains and dependencies inventoried.

Findings to address:

- NRC creates threads for every parallel pass and serializes each work dispatch
  under the compiler's global lock.
- The active thread implementation has a fixed 64-element array while automatic
  hardware detection is not bounded to that array.
- Decompiler triangle lookup stops at a maximum bound, potentially missing larger
  triangles that still overlap the brush face.
- Upstream uses a monolithic Makefile; a standalone CMake build is needed.

No performance gains or finished GUI/GPU features are claimed at this stage.
Next task: import the required source subset and produce the baseline executable.

## 2026-09-29 — Standalone compiler source import

Imported 209 files from the pinned NRC revision, including compiler sources,
transitive local dependencies, image/network support, upstream regression fixtures,
and license/contributor texts. Added an original-file SHA-256 provenance manifest.
Excluded the Radiant editor and bundled Assimp implementation.

Validation: verified every imported file against the source reference by SHA-256.
Compiler and bundled component license notices were checked for compatibility.
No compiler behavior was changed. Build validation is the next task.

## 2026-09-29 — Component license audit correction

The full file-level audit found Apache-2.0 ETC code and BSD-3-Clause WebP code;
the initial documentation incorrectly described both as MIT. Corrected the labels,
included missing license texts, and selected GPL-3.0-or-later for the combined
project using q3map2's existing later-version permission. Original file notices
remain unchanged. This is an inherited licensing detail, not a compiler change.

Validation: checked actual file notices and Apache's published GPL compatibility
guidance; retained the original import manifest unchanged.

## 2026-09-29 — Standalone release build

Added CMake/Ninja release, debug, and profile presets, explicit system dependencies,
CTest help/game smoke checks, optional LTO and sanitizer switches, and license
installation rules. No imported compiler source changes were needed.

Validation: Windows x64 release built with MSYS2 GCC 15.2.0; 2/2 CTest tests passed.
Preserved the baseline binary at `build/baseline/bin/q3mapx.exe` (SHA-256
`5BEE5356D9B4D0C94A80B6E206C940340DB11F807FE44B416EEB366AD210336E`).
Build log: `.agents/tmp/bootstrap/build.log`. Dependencies: GLib 2.86.2, libxml2
2.15.1, Assimp 6.0.2, PNG 1.6.51, zlib 1.3.1, libjpeg ABI 80.

Additional findings: this MSYS2 Assimp pkg-config file contains non-relocatable
paths; using its CMake config resolves that packaging issue. Upstream produces
warnings for non-standard-layout `offsetof` and direct libxml buffer access.
Linux and MSVC execution are not locally validated yet. Next: asset-independent
integration fixtures and timing baseline.

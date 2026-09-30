# MAP and script input checks

Development builds after 0.3.0 reject empty MAP files, a missing initial
`worldspawn`, incomplete entities/brushes and unexpected primitive or trailing
tokens. Missing entity values cannot consume an unquoted brace. Unsupported
`terrainDef` source primitives are errors; this does not change native MOHAA BSP
terrain recovery. Entity-only updates, LIGHT source loading and MAP-input
conversion use these checks too.

Structural braces and matrix delimiters must be unquoted. Quoted `{`, `}` and
`$include` remain literal entity keys/values and survive BSP decompilation.
Brush-primitive editor metadata is read as key/value pairs with checked EOF.
An incomplete source now produces an error instead of continuing with a partial
entity list or attempting to access a nonexistent world entity.

## Script includes

File scripts support unquoted `$include` followed by a nonempty filename on the
same line. Quoted filenames can contain spaces. Paths use the existing game VFS,
including PK3 entries; they are not relative to the including file's directory.
Fragments may contain part of a MAP entity or shader definition. For example:

```text
$include "maps/room-fragment.inc"
```

A requested include that cannot be loaded is fatal. Include processing is
iterative, including returns through empty files and consecutive directives.
Each top-level file load has these limits, with the root counted:

- 64 simultaneously active files;
- 1,024 total file loads, counting repeated and empty includes;
- 256 MiB of cumulative source bytes, counting each repeated load.

The remaining byte budget is passed to the VFS before allocating or decompressing
a source buffer. Cycles, including path aliases and packed files, terminate at
these limits. Optional top-level script lookups keep their existing missing-file
return behavior. Diagnostics restore the parent's resolved location when an
include ends, including the archive member name.

`ParseFromMemory`, used for BSP entities, does not expand includes. Quoted
directive text is ordinary data; an unquoted directive where an entity opening
brace is expected is rejected without loading its named file.

## Patches

Development builds after 0.3.0 validate `patchDef2` dimensions before converting
them to integers, multiplying them or allocating the control-point mesh. Invalid
input exits with an error instead of truncating a fractional dimension, allocating
an invalid size or silently interpreting a numeric prefix.

Patch width and height must be odd integers from **3 through 31**. Decimal and
scientific spellings that represent those integers exactly remain accepted, such
as `3`, `3.0`, `+3e0` and `300e-2`. A fractional suffix too small to survive normal
floating-point rounding is still rejected. The three other header fields retain
their legacy ignored meaning, but must be finite decimal numbers.

Control-point positions must be finite and lie within the compiler's world range,
**−65,536 through 65,536**. Texture coordinates must fit finite binary32 storage;
nonzero values that would become zero through underflow are rejected. Signs,
decimal fractions and scientific notation remain supported. Trailing junk, NaN,
infinity, hexadecimal spellings and missing/extra matrix elements are errors.
Position and texture arrays are assigned separately, and all other vertex channels
are initialized before interpolation. Existing geometrically degenerate-patch
diagnostics and skipping behavior remain separate from these syntax checks.

An error identifies the entity, primitive, line and source location where the
patch reader detects it, for example:

```text
Invalid MAP patch (entity 0, primitive 0) at line 8 in .../fixture.map:
expected odd integer patch width in 3..31, got '3.5'
```

Tokenizer errors identify the line/file or incomplete line. Its existing maximum
is 1,023 token bytes plus a terminator; the bound now also applies when a token
ends exactly at file EOF. Unterminated quotes/comments and NUL bytes inside quoted
tokens are rejected. These tokenizer checks also protect other consumers of the
shared script reader. Material names retain their separate, shorter name limits.

## Failure and update behavior

BSP compilation now waits for source loading to succeed before discarding old
PRT, LIN and saved REG files. Source parse failures preserve the previous BSP and
those sidecars. Successful `-onlyents` updates retain geometry sidecars because
they do not rebuild geometry. Region input and editor `-tempname` input survive
the tested failures. See [output guarantees](OUTPUT-SAFETY.md) for publication
after compilation has begun; this is not a transaction over every compiler output.

The strict patch reader also applies during `-nocurves`, entity-only updates,
LIGHT's source-entity load and MAP-input conversion. Skipping patch geometry does
not bypass structural/numeric validation. Brush numeric matrices and texture
parameters still use permissive legacy parsing and need a separate audit.
Optional-token lookahead across included files and line accounting inside
multiline quoted tokens also remain outside this round's validation.
Remaining raw sidecar writers also retain their documented limits. No general
untrusted-MAP sandbox or complete parser-safety claim is made.

## Validation

The `script_input` regression covers 24 valid native-source cases across IBSP/RBSP,
Quake/brush-primitive/Valve 220 syntax, loose/packed includes, fragments, shader
includes, exact depth/file-count boundaries, literal keys/values and metadata.
Optional `--reference` compares every stored BSP lump for the 21 cases also
supported by the preceding compiler. Literal special tokens and brush metadata
are checked directly with the repaired parser, including native BSP decompilation.

Another 145 cases reject empty/incomplete entities, missing values, truncation
through each first-brush-side token, include cycles and exceeded depth/file/byte
budgets, wrong primitive types, forbidden BSP include expansion and malformed
BSP entity data. They verify controlled errors, unchanged input and previous
outputs, including LIGHT, entity-only, conversion, region and editor-temporary
source paths. The byte-limit test repeatedly loads a 1 MiB comment file; it does
not request an oversized allocation from the preceding compiler.

See [script/entity test instructions](DEVELOPMENT.md#script-and-entity-input-validation)
and [recorded evidence](validation/script-input.json).

The `patch_input` regression exercises 44 valid native IBSP/RBSP builds, including
minimum/maximum/rectangular patches, ordinary/meta modes, scientific notation,
legacy metadata, comments and maximum-length tokens. Optional `--reference`
requires every stored BSP lump to match the preceding compiler for those cases.
It does not mask geometry or numeric fields.

Another 201 cases cover invalid dimensions/numbers, numeric underflow/overflow,
matrix shape errors, truncation at each patch token boundary, quotes/comments,
NULs, token limits and the affected command modes. They require a normal error
exit, no sanitizer report or allocation-failure fallback, unchanged source and
unchanged previous BSP/PRT/LIN/SRF/REG/OBJ/MTL outputs where applicable. The positive
entity-only control must preserve all non-entity BSP lumps and its sidecars.

See [test instructions](DEVELOPMENT.md#map-patch-input-validation) and
[recorded evidence](validation/patch-input.json). These generated fixtures do not
establish compatibility with every existing author's MAP file.

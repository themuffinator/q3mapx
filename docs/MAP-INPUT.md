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

## Brushes

Quake, brush-primitive and Valve 220 brush readers require complete finite decimal
numbers for plane points and texture parameters. Signs, decimal fractions,
scientific notation and quoted numeric tokens remain accepted. Numeric prefixes,
NaN, infinity, hexadecimal spellings and out-of-range values are errors. Texture
parameters must fit finite binary32 storage without nonzero values underflowing
to zero. The Valve rotation field remains unused but is validated too.

Plane points are read in double precision. They define infinite planes and are
not clamped to the world bounds: distant points can describe a perfectly ordinary
in-bounds brush. The resulting cross product, normalization and plane distance
must remain finite, and the distance must fit binary32 storage. Hashing large
finite plane distances avoids an out-of-range integer conversion; this does not
extend the compiler's supported world size.

Zero Quake/Valve texture scales retain their historical fallback to one. Negative
scales and zero brush-primitive matrices remain supported. Derived texture axes
must be finite. When constructing brush geometry, the compiler also checks UVs
on the actual brush windings before source loading completes. Flat mapping and
shader-generated coordinates bypass this source-mapping preflight. Final surface
emission checks the coordinates it actually produces, including those other
mapping paths and any later geometry transformations.

The optional three legacy flag/value fields must all be present together. They
accept decimal integers in **−2,147,483,648 through 4,294,967,295**, accommodating
signed and unsigned editor spellings of 32-bit values. Fractional/exponent forms,
overflow and trailing text are errors. Only the historical detail bit in the
first field affects compilation; the other fields retain their ignored meaning.

Degenerate sides are still reported and removed after their syntax is validated.
Quake texture projection no longer indexes a nonexistent plane before that
removal. Raw numeric errors identify the entity, primitive, source side and
detection line/location; derived winding errors identify the source primitive.

## LIGHT source loading

LIGHT uses BSP geometry and rereads MAP light entities unless `_keepLights` is
set. It still validates source brushes and patches, including their numeric
fields, even though those primitives are discarded. Source brush materials no
longer resolve images or borrow the first shader's flags/dimensions. Quake texture
shift rebiasing only runs when constructing brush geometry; raw parameters and
derived texture axes remain checked during LIGHT.

Previously an unrelated first shader could have zero, unfinished dimensions,
causing a valid MAP to fail with `expected finite derived texture mapping`.
An empty shader table also made that lookup invalid. Relighting now works without
depending on shader declaration order or SRF material initialization. The source
MAP can contain materials that are absent from the compiled BSP; they are ignored
by this entity-only read. Materials actually used by the BSP still resolve through
the normal lighting path.

The `light_source` regression compares all resulting BSP lump payloads against a
MAP containing only the same light entities. It covers 72 combinations of Q3/JA,
Quake/brush-primitive/Valve syntax, legacy/authored brushes, used-first/unused-first/
empty shader definitions and SRF records with/without material names. All three
patch source formats are included. Twelve no-light controls must change the bake;
62 malformed-source cases must report their actual invalid field or incomplete
line and preserve previous outputs. Optional `--reference` compares the no-brush
oracle with a previous compiler without exercising its unsafe brush lookup.

See [test instructions](DEVELOPMENT.md#light-source-loading) and
[recorded evidence](validation/light-source.json). This fixes source loading;
it does not expand `_keepLights` semantics or make MAP geometry affect a relight.

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
not bypass structural/numeric validation. Brush raw-number checks likewise apply
to entity-only, LIGHT, MAP-input conversion and discarded detail geometry. Winding
UV checks run when source brush geometry is constructed; LIGHT's entity-only
source load does not construct it. A later surface-emission failure remains
outside the guarantee for errors during initial source loading.
Optional-token lookahead across included files and line accounting inside
multiline quoted tokens also remain outside this round's validation.
Remaining raw sidecar writers also retain their documented limits. No general
untrusted-MAP sandbox or complete parser-safety claim is made.

## Validation

The `brush_input` regression covers 116 valid native IBSP/RBSP builds with Quake,
brush-primitive and Valve 220 input, ordinary/meta compilation, decimal/scientific
and quoted numbers, legacy flags, zero/negative scales and distant defining
points. Optional `--reference` compares all stored BSP lumps for 110 cases. Six
degenerate-side cases instead compare against their clean-geometry controls so
the preceding unsafe access is never invoked by reference comparison.

Another 242 cases reject malformed point/texture/flag fields, arithmetic overflow
and affected command modes. They require controlled errors, unchanged sources
and previous BSP/PRT/LIN/SRF/REG/OBJ/MTL outputs where applicable. Two entity-only
controls exercise large finite plane hashes while preserving every non-entity
BSP lump and geometry sidecar. They are not giant-world compilation tests.

See [brush test instructions](DEVELOPMENT.md#map-brush-input-validation) and
[recorded evidence](validation/brush-input.json).

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

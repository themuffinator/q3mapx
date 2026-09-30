# Checked output publication

BSP saves and shared `SaveFile` buffer saves write to reserved sibling files and
publish only after all writes and the final close succeed. A reported write,
seek, flush or replacement failure preserves the previous destination. The
output owner closes streams before removing unfinished `.q3mapx-*.tmp` files,
including on Windows, where an open stream can prevent their removal. The CLI
reports the destination and failure, exits nonzero, and emits no success message
for that failed save.

This covers the IBSP and Raven serializers, their associated game profiles, and
the profiles, compute reports, JSON records and other buffers written through
`SaveFile`. All native BSP commands share that writer, including BSP, VIS, LIGHT,
scale and conversion. Existing destinations must be regular files; directory
and symbolic-link outputs are rejected. Input files and unrelated directory
contents are not removed during cleanup.

VIS removes its PRT only after successful BSP publication. If the BSP cannot be
saved, the preceding BSP and PRT remain available for retry; `-saveprt` also keeps
the PRT after a successful run. A profile is a separate output: a failure saving
it does not roll back a BSP already published by that command.

BSP compilation defers deletion of previous PRT, LIN and saved REG files until
source loading succeeds. Source parse errors therefore retain them for inspection
or retry; successful `-onlyents` updates leave geometry sidecars in place too.
The [MAP/script-input checks](MAP-INPUT.md) cover prior BSP/SRF and mesh outputs as
well, including patch, entity/brush-structure and include errors during
LIGHT/conversion, region files and editor temporary sources. Failures
later in compilation and other raw sidecar writes remain separate concerns.

MAP/recovery-report and OBJ/MTL exports already use the same output owner. Their
ordered publication and rollback behavior are described in the
[decompilation guide](DECOMPILATION.md). This change does not turn all files from
a compilation into one transaction. Legacy raw-file writers such as some
sidecars remain outside this guarantee. Process termination, power loss and
concurrent external modification are not covered by reported-error cleanup;
staging files can remain after a killed process. Filesystem rename atomicity
does not establish power-loss durability.

## Implementation

`OutputFiles` owns every staged stream. Its borrowed `FILE*` may be written by
native serializers but must not be closed by them. `writeOutput`, `tellOutput`
and `seekOutput` throw on invalid arguments or I/O errors. Seek and tell use
64-bit positions; BSP lump checks retain the existing signed 2 GiB format limit.
Header rewriting and padding order are unchanged. The BSP size message is now
printed after successful close and publication.

Fatal `Error` still terminates immediately because worker threads can be live.
The BSP and `SaveFile` boundaries catch exceptions only after their output owner
has unwound. Legacy shader remapping occurs before the output owner exists,
and abstract BSP byte order is restored after a failed serialization/publication.
The unused fatal `SafeWrite`/`SafeClose` helpers were removed to avoid reintroducing
the destructor-bypass path.

## Regression checks

- `atomic_file`: binary overwrite bytes, positions above 32 bits
  without allocating a huge file, rejected arguments, real read-only stream
  errors, original preservation and abandoned-stage cleanup.
- `binary_outputs`: empty-world, ordinary and large-lightmap IBSP/RBSP rewrites;
  Windows denied replacements; POSIX short writes, buffered header-seek and
  close failures; directory/link guards; successful retries and read-back.
  An optional `--reference` compares every successful file byte after masking
  only the unused timestamp, including directory offsets and padding.
- `vis_merge`: a failed VIS save preserves BSP/PRT and leaves no new staging file;
  a successful retry consumes the PRT. The former test-side cleanup workaround
  has been removed.
- Existing pipeline, Raven grid, profile, minimap, MAP/report and mesh export
  checks exercise consumers of the shared writer.

See [validation results](validation/checked-writes.json). These are correctness
checks, not a compiler performance claim. Windows link-creation checks may be
skipped when privileges are unavailable; Linux exercises those guards.

# Numeric command-line input

Options using the shared integer, double and float parsers require a complete
numeric argument. They accept at most one leading sign. In particular, `+-1`
and `+-0` are errors rather than alternate spellings of negative one and zero.
Development builds after 0.3.0 repair the leading-plus normalization that could
previously admit these double signs.

| Kind | Accepted examples | Rejected examples |
| --- | --- | --- |
| Integer | `8`, `+008`, `-1`, `-0` | `+-1`, `++1`, `1.0`, `1e0`, `0x10` |
| Finite decimal | `.5`, `+1.`, `-1.25e+1`, `+1e-5` | `+-.5`, `1e+-1`, `nan`, `inf`, `0x1` |

The option's existing range still applies. For example, `-threads +004` means
four workers, while `-threads -1` is outside the allowed range. Leading zeroes
remain decimal. Empty arguments, sign-only arguments, surrounding whitespace,
trailing text and integer/double parsing overflow or underflow are rejected. Some
individual stages clamp otherwise valid values after parsing; this repair does
not change those stage semantics or existing float conversion behavior.

Diagnostics identify the option, expected type/range and supplied text. Checked
callers include common worker/patch settings, BSP controls, LIGHT, minimaps,
decompilation and BSP evidence. Invalid numeric options exit with status 1.
The regression matrix verifies that its selected callers retain the original
MAP/BSP and existing PRT/SRF/LIN/REG, recovered MAP/report, minimap and diagnostic
report files when parsing fails.

This is the shared option-parser contract, not an assertion that every legacy
positional argument has been migrated. BSP scale/shift components still use
their inherited positional parsing and need a separate audit. MAP/script tokens
have their own [input rules](MAP-INPUT.md).

See [reproduction instructions](DEVELOPMENT.md#numeric-cli-validation) and
[recorded validation](validation/cli-numbers.json). Native successful builds,
lighting, minimaps and decompilation compare equivalent signed spellings and
optionally compare the preceding compiler. Differently spelled arguments retain
their different `_q3map2_cmdline` provenance; all other entity bytes and BSP lumps
must match. Identically spelled reference runs require exact stored BSP lumps
and complete MAP/TGA output.
These are correctness checks; no
performance gain is claimed.

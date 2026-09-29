# SPDX-License-Identifier: GPL-3.0-or-later
"""Retained heuristic analysis must not dereference guessed ranges."""
import argparse
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    path = root / "analysis.bsp"
    prefix = b"TEST" + struct.pack("<i", 1)

    def run(data, expected, *options):
        path.write_bytes(data)
        result = subprocess.run([str(exe), "-fs_basepath", str(root), "-fs_homepath", str(root / "home"),
                                 "-threads", "1", "-analyze", *options, str(path)],
                                cwd=root, capture_output=True, timeout=20)
        assert result.returncode == expected, (result.returncode, result.stdout, result.stderr)
        assert path.read_bytes() == data
        if expected:
            assert b"Invalid BSP analysis" in result.stdout
        return result.stdout.replace(b"\r\n", b"\n")

    for swap in (False, True):
        options = ("-lumpswap",) if swap else ()
        def entry(offset, length):
            return struct.pack("<ii", length, offset) if swap else struct.pack("<ii", offset, length)
        for data in (b"", b"TEST", prefix + b"\0", prefix + entry(-1, 4),
                     prefix + entry(16, -1), prefix + entry(16, 0x7fffffff),
                     prefix + entry(4, 4), prefix + entry(0x7fffffff, 0x7fffffff)):
            run(data, 1, *options, "-force")
        # No four-byte scalar exists in a one-, two- or three-byte final lump.
        for payload in (b"{", b"ab", b"abc", b"abcd"):
            run(prefix + entry(16, len(payload)) + payload, 0, *options)
        run(prefix + entry(17, 4) + b"xabc\0", 0, *options)  # unaligned payload
        out = run(prefix + entry(0, 0) + entry(24, 4) + b"xyz\0", 0, *options)
        assert b"As string:     xyz\n" in out and b"Lump count:    2" in out
        # First range is valid but the following guessed one is not.
        run(prefix + entry(24, 4) + entry(99, 5) + b"xy\0\0padding", 1, *options)
    print("Legacy analysis: truncated headers, guessed ranges, short/unaligned data and swapped entries passed")


if __name__ == "__main__":
    main()

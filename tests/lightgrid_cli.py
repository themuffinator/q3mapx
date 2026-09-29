"""Raven grid serialization through the real CLI. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
from pathlib import Path
import struct
import subprocess

from fixtures import create_fixture
from integration import run


def lumps(data):
    assert data[:4] in (b"RBSP", b"FBSP")
    return [data[offset:offset + length] for offset, length in
            (struct.unpack_from("<ii", data, 8 + i * 8) for i in range(18))]


def with_grid(data, points, indices):
    payloads = lumps(data)
    payloads[15] = b"".join(points)
    payloads[17] = struct.pack(f"<{len(indices)}H", *indices)
    result = bytearray(data[:152])
    for i, payload in enumerate(payloads):
        struct.pack_into("<ii", result, 8 + i * 8, len(result), len(payload))
        result.extend(payload)
        result.extend(b"\0" * (-len(result) % 4))
    return bytes(result)


def point(value=0, direction=0, style=0):
    return bytes([value]) * 24 + struct.pack("<I", style) + bytes([direction, direction])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    source = create_fixture(root)
    base = ["-game", "ja", "-fs_basepath", root, "-fs_basegame", "baseq3", "-threads", 2]
    run(exe, [*base, "-meta", source], root, "compile")
    original = source.with_suffix(".bsp").read_bytes()
    # Earliest approximate matches survive multiple candidate buckets and the
    # circular direction seam. In particular value 4 chooses 8 before 0.
    palette = [point(8, 0), point(0, 255), point(4, 251), point(8, 250), point(8, 5)]
    indices = [0, 1, 2, 3, 4] * 300
    path = root / "grid.bsp"
    path.write_bytes(with_grid(original, palette, indices))
    run(exe, [*base, "-scale", 1, path], root, "rewrite")
    output = lumps((root / "grid_s.bsp").read_bytes())
    assert output[15] == b"".join(palette[i] for i in (0, 1, 3, 4))
    assert output[17] == struct.pack("<1500H", *([0, 1, 0, 2, 3] * 300))
    run(exe, [*base, "-decompile", "-o", root / "recovered.map", root / "grid_s.bsp"], root, "read-back")

    # The input file may represent 65536 unique entries with uint16 indices.
    # NRC's writer only admits 65535. Exhaustion must not replace prior output.
    boundary = [point(style=i) for i in range(65536)]
    path.write_bytes(with_grid(original, boundary, list(range(65536))))
    before = path.read_bytes()
    destination = root / "grid_s.bsp"
    destination.write_bytes(b"preserve-existing-output")
    result = subprocess.run([str(exe), *map(str, base), "-scale", "1", str(path)],
                            cwd=root, capture_output=True, timeout=30)
    (root / "overflow.log").write_bytes(result.stdout + result.stderr)
    assert result.returncode == 1 and b"65535 distinct samples" in result.stdout, result.stdout
    assert destination.read_bytes() == b"preserve-existing-output"
    assert path.read_bytes() == before
    assert not list(root.glob("*.tmp*")), "Failed rewrite left a partial temporary BSP"
    print("Native Raven grid rewrite, first-match parity, read-back and atomic overflow failure passed")


if __name__ == "__main__":
    main()

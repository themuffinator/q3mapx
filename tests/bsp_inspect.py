# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent native-directory fixtures; inspection never claims geometry validity."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import struct
import subprocess


IBSP = [0, 72, 16, 36, 48, 4, 4, 40, 12, 8, 44, 4, 72, 104, 0, 8, 0]
RAVEN = [0, 72, 16, 36, 48, 4, 4, 40, 12, 12, 80, 4, 72, 148, 0, 30, 0, 2]
RITUAL = [76, 16, 0, 108, 44, 4, 4, 4, 48, 36, 8, 12, 72, 40, 0, 0, 8, 0, 0, 52]
MOHAA = [140, 16, 0, 108, 44, 4, 4, 4, 64, 36, 32, 12, 12, 40, 0, 0, 0, 2, 0, 56, 4, 0, 388, 2, 0, 164, 2, 0]
EARLY = [0, 20, 36, 48, 4, 4, 48, 12, 8, 0, 0, 44, 156, 68]
V45 = IBSP.copy()
V45[2], V45[7], V45[12] = 20, 56, 68
V44 = EARLY.copy() + [4]
V44[12] = 164
FORMATS = [
    (b"IBSP", 46, 8, "ibsp46", IBSP), (b"IBSP", 47, 8, "ibsp47", IBSP),
    (b"IBSP", 47, 8, "quakelive", IBSP + [128]),
    (b"RBSP", 1, 8, "rbsp1", RAVEN), (b"FBSP", 1, 8, "fbsp1", RAVEN),
    (b"FAKK", 12, 12, "fakk12", RITUAL), (b"FAKK", 42, 12, "fakk42", RITUAL),
    (b"2015", 19, 12, "mohaa19", MOHAA),
    (b"IBSP", 43, 8, "ibsp43", EARLY), (b"IBSP", 44, 8, "ibsp44", V44),
    (b"IBSP", 45, 8, "ibsp45", V45),
]


def make_file(ident, version, offset, sizes):
    header = ident + struct.pack("<i", version)
    if offset == 12:
        header += struct.pack("<I", 0xfedcba98)
    directory = bytearray(len(sizes) * 8)
    data = bytearray(header) + directory
    for index, size in enumerate(sizes):
        # Deliberate unaligned offsets are legal. Variable data has no record count.
        data.extend(b"\0")
        struct.pack_into("<ii", data, offset + index * 8, len(data), size or 7)
        data.extend(bytes(size or 7))
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)

    def inspect(path, *options, expected=0):
        before = hashlib.sha256(path.read_bytes()).digest() if path.exists() else None
        p = subprocess.run([str(exe), "-inspect", "-json", *options, str(path)], cwd=root,
                           capture_output=True, timeout=10)
        assert p.returncode == expected, (p.returncode, p.stdout, p.stderr)
        report = json.loads(p.stdout)
        assert report["schema_version"] == 1 and report["geometry_validated"] is False
        assert report["valid"] == (expected == 0)
        if before is not None:
            assert hashlib.sha256(path.read_bytes()).digest() == before
        return report

    for ident, version, offset, name, sizes in FORMATS:
        data = make_file(ident, version, offset, sizes)
        path = root / (name + " with spaces.bsp")
        path.write_bytes(data)
        report = inspect(path)
        layout = next(item for item in report["layouts"] if item["id"] == name)
        assert layout["valid"] and layout["header_bytes"] == offset + len(sizes) * 8
        assert [lump["record_bytes"] for lump in layout["lumps"]] == sizes
        assert all(lump["records"] == (1 if size else None) for lump, size in zip(layout["lumps"], sizes))
        if offset == 12:
            assert layout["stored_checksum"] == 0xfedcba98
        if ident == b"RBSP":
            assert report["ambiguous_game"] and set(report["profile_candidates"]) == {"ja", "jk2", "sof2"}
            assert inspect(path, "-game", "JKA-SP")["selected_profile"] == "ja"
        if name == "ibsp47":
            assert len(report["layouts"]) == 2
            assert inspect(path, "-game", "rtcw-sp")["layouts"][0]["id"] == "ibsp47"
            inspect(path, "-game", "ql", expected=1)
        if name == "quakelive":
            assert inspect(path, "-game", "ql")["layouts"][0]["id"] == "quakelive"
        # Damage every native layout's first nonempty record; both game flags and
        # -force must never route inspection through unchecked legacy parsing.
        if name == "quakelive":
            selected = ["-game", "ql"]
        else:
            selected = []
        for label, start, length in (("negative", -1, 16), ("negative-length", 180, -1),
                                      ("outside", len(data) - 1, 200), ("header", 4, 4),
                                      ("huge", 0x7fffffff, 0x7fffffff)):
            broken = bytearray(data)
            struct.pack_into("<ii", broken, offset, start, length)
            bad = root / "broken.bsp"
            bad.write_bytes(broken)
            inspect(bad, *selected, expected=1)
        print(name, "native directory and malformed ranges passed", flush=True)

    path = root / "broken.bsp"
    data = make_file(b"IBSP", 46, 8, IBSP)
    struct.pack_into("<ii", data, 8 + 2 * 8, *struct.unpack_from("<ii", data, 8 + 3 * 8))
    path.write_bytes(data)
    assert any("overlaps lump" in e for e in inspect(path, expected=1)["layouts"][0]["errors"])
    for data in (b"", b"IBSP", b"IBSP" + struct.pack("<i", 46),
                 b"\xff\x00\x1b\xfe" + struct.pack("<i", 19),
                 b"IBSP" + struct.pack("<i", 999)):
        path.write_bytes(data)
        inspect(path, expected=1)
    path.write_bytes(make_file(b"IBSP", 46, 8, IBSP))
    inspect(path, "-game", "ja", expected=1)
    inspect(path, "-game", "unknown", expected=1)
    inspect(root / "missing.bsp", expected=1)
    # Header-only parsing remains bounded on large input. POSIX truncate is
    # sparse; keep the Windows fixture small enough for non-sparse volumes.
    large_bytes = 64 * 1024**2 if os.name == "nt" else 3 * 1024**3
    large = root / "sparse.bsp"
    with large.open("wb") as file:
        file.write(make_file(b"IBSP", 46, 8, IBSP))
        file.truncate(large_bytes)
    # No whole-file hashing for this one: the inspection itself must read <=256 bytes.
    p = subprocess.run([str(exe), "-inspect", "-json", str(large)], cwd=root, capture_output=True, timeout=5)
    assert p.returncode == 0 and json.loads(p.stdout)["file_bytes"] == large_bytes
    large.unlink()
    rng = random.Random(42)
    for _ in range(40):
        path.write_bytes(rng.randbytes(rng.randrange(8, 260)))
        inspect(path, expected=1)
    p = subprocess.run([str(exe), "-inspect", str(root / "rbsp1 with spaces.bsp")], cwd=root, capture_output=True, timeout=10)
    assert p.returncode == 0 and b"Shared signature" in p.stdout
    print("Inspection, ambiguity, unaligned directories, malformed input and bounded reads passed")


if __name__ == "__main__":
    main()

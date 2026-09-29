"""Raven inactive lightmap slots and shared references. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess

from fixtures import create_fixture
from integration import run
from lightgrid_cli import lumps


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    source = create_fixture(root)
    base = ["-game", "ja", "-fs_basepath", root, "-fs_basegame", "baseq3", "-threads", 2]
    run(exe, [*base, "-meta", source], root, "compile")
    run(exe, [*base, "-light", "-fast", source], root, "light")
    original = source.with_suffix(".bsp").read_bytes()
    vertex_offset, _ = struct.unpack_from("<ii", original, 8 + 10 * 8)
    surface_offset, surface_size = struct.unpack_from("<ii", original, 8 + 13 * 8)
    surfaces = [surface_offset + i for i in range(0, surface_size, 148)
                if struct.unpack_from("<i", original, surface_offset + i + 16)[0] > 0]
    first_vertex = struct.unpack_from("<i", original, surfaces[0] + 12)[0]
    bad_vertex = vertex_offset + first_vertex * 80
    assert original[surfaces[0] + 29:surfaces[0] + 32] == b"\xff\xff\xff"
    path = root / "unused.bsp"

    for slot in range(4):
        data = bytearray(original)
        if slot == 0:
            # Vertex-lit patches still carry unused primary lightmap fields.
            for surface in surfaces:
                first, count = struct.unpack_from("<ii", data, surface + 12)
                if first <= first_vertex < first + count:
                    struct.pack_into("<i", data, surface + 36, -3)
        struct.pack_into("<2f", data, bad_vertex + 20 + slot * 8, math.nan, math.inf)
        path.write_bytes(data)
        run(exe, [*base, "-scale", 1, path], root, f"unused-{slot}")
        changed = lumps((root / "unused_s.bsp").read_bytes())[10]
        expected = bytearray(lumps(original)[10])
        struct.pack_into("<2f", expected, first_vertex * 80 + 20 + slot * 8, 0, 0)
        assert changed == expected, "Normalization changed a used or finite coordinate"
        run(exe, [*base, "-decompile", "-o", root / "recovered.map", path], root, f"recover-{slot}")
        report = json.loads((root / "recovered.map.recovery.json").read_text())
        assert report["normalized_unused_lightmap_uv_pairs"] == 1
        assert path.read_bytes() == data

    # Any active coordinate remains strict. A different
    # surface sharing the same vertices also makes an otherwise unused slot live.
    for name, slot, active_surface in (("primary", 0, None), ("active", 1, surfaces[0]),
                                        ("shared", 2, surfaces[1])):
        data = bytearray(original)
        struct.pack_into("<f", data, bad_vertex + 20 + slot * 8, math.nan)
        if active_surface is not None:
            data[active_surface + 28 + slot] = 0
            struct.pack_into("<i", data, active_surface + 36 + slot * 4, 0)
            struct.pack_into("<ii", data, active_surface + 12, first_vertex, 1)
            struct.pack_into("<i", data, active_surface + 24, 0)
            struct.pack_into("<i", data, active_surface + 8, 1)
        path.write_bytes(data)
        for force in ([], ["-force"]):
            output = root / "unused_s.bsp"
            output.write_bytes(b"preserve existing BSP")
            result = subprocess.run([str(exe), *map(str, base), *force, "-scale", "1", str(path)],
                                    cwd=root, capture_output=True, timeout=30)
            (root / f"{name}-{'force' if force else 'strict'}.log").write_bytes(result.stdout + result.stderr)
            assert result.returncode == 1 and b"lightmap coordinate" in result.stdout, result.stdout
            assert output.read_bytes() == b"preserve existing BSP"

    # Reproduce the retail zero-geometry flare record without retail assets.
    assert not lumps(original)[12], "Fixture must have no fogs"
    data = bytearray(original)
    surface = surfaces[-1]
    struct.pack_into("<iii", data, surface + 4, 0, 4, 0)
    struct.pack_into("<iii", data, surface + 16, 0, 0, 0)
    path.write_bytes(data)
    run(exe, [*base, "-decompile", "-o", root / "flare.map", path], root, "flare-without-fog")
    report = json.loads((root / "flare.map.recovery.json").read_text())
    assert report["normalized_unused_flare_fogs"] == 1
    # The same invalid fog on actual geometry is not covered by this repair.
    data = bytearray(original)
    struct.pack_into("<i", data, surface + 4, 0)
    path.write_bytes(data)
    result = subprocess.run([str(exe), *map(str, base), "-force", "-scale", "1", str(path)],
                            cwd=root, capture_output=True, timeout=30)
    assert result.returncode == 1 and b"surface fog" in result.stdout, result.stdout
    print("Unused Raven slots/empty flare repaired; active, shared and geometry references remain strict")


if __name__ == "__main__":
    main()

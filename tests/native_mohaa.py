# SPDX-License-Identifier: GPL-3.0-or-later
"""Native MOHAA records, terrain holes, fence metadata and static placements."""
import argparse
import json
from pathlib import Path
import struct
import subprocess

from fixtures import create_fixture
from integration import Bsp, run


def pack(lumps):
    data = bytearray(b"2015" + struct.pack("<iI", 19, 0xabcdef01) + bytes(28 * 8))
    for index, lump in enumerate(lumps):
        data += b"\0"  # exercise unaligned extension records
        struct.pack_into("<ii", data, 12 + index * 8, len(data), len(lump))
        data += lump
    return data


def terrain(alternate=False):
    data = bytearray(388)
    struct.pack_into("<4B8fbbhHH4h", data, 0, 128 if alternate else 0, 1, 0, 0,
                     0, 0, 0, 1, 1, 0, 1, 1, -1 if not alternate else 7, -2, 50, 0, 65535, -1, -1, -1, -1)
    struct.pack_into("<H", data, 52 + 31 * 2, 0x2000)
    data[304:385] = bytes(range(81))
    return bytes(data)


def native_lumps(bsp):
    shaders = bytearray()
    for i in range(0, len(bsp.lump(1)), 72):
        shaders += bsp.lump(1)[i:i+72] + struct.pack("<i", 16) + b"textures/q3mapx/checker\0".ljust(64, b"\0")
    # Give a real brush shader fence contents, with valid equations for all sides.
    flags = struct.unpack_from("<I", shaders, 68)[0]
    struct.pack_into("<I", shaders, 68, flags | 0x2000)
    surfaces = b"".join(bsp.lump(13)[i:i+104] + struct.pack("<f", 8.0) for i in range(0, len(bsp.lump(13)), 104))
    leafs = b"".join(bsp.lump(4)[i:i+48] + struct.pack("<4i", 0, 2, 0, 1) for i in range(0, len(bsp.lump(4)), 48))
    sides = b"".join(bsp.lump(9)[i:i+8] + struct.pack("<i", 0) for i in range(0, len(bsp.lump(9)), 8))
    placement = struct.pack("<128s7f2i", b"models/test.tik", 1, 2, 3, 10, 20, 30, 1.5, 0, 1)
    return [bytes(shaders), bsp.lump(2), bsp.lump(14), surfaces, bsp.lump(10), bsp.lump(11),
            bsp.lump(6), bsp.lump(5), leafs, bsp.lump(3), struct.pack("<8f", 1, 0, 0, 5, 0, 1, 0, 7),
            sides, bsp.lump(8), bsp.lump(7), bsp.lump(0), bsp.lump(16), bytes(768), bytes(2), bytes(2),
            bytes(56), bytes(4), bytes(4), terrain() + terrain(True), struct.pack("<2H", 0, 1),
            b"\xff\x80\x40", placement, bytes(2), b""]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    source = create_fixture(root)
    create_fixture(root, game_directory="main")
    common = ["-fs_basepath", root, "-fs_homepath", root / "home", "-threads", 2]
    run(exe, ["-game", "quake3", *common, "-meta", source], root, "fixture")
    bsp = Bsp(source.with_suffix(".bsp"))
    lumps = native_lumps(bsp)
    path = root / "native.bsp"
    data = pack(lumps); path.write_bytes(data)
    base = ["-game", "mohaa", *common]
    run(exe, [*base, "-info", path], root, "info")
    assert "Recovered 254 terrain triangles; 2 removed" in (root / "info.log").read_text()
    output = root / "recovered.map"
    run(exe, [*base, "-decompile", "-o", output, path], root, "recover")
    report = json.loads(Path(str(output) + ".recovery.json").read_text())
    assert report["game"] == "mohaa" and report["brushes"] == 8 and report["patches"] == 1
    assert report["native_terrain_triangles"] == 254 and report["native_terrain_removed_triangles"] == 2
    assert report["native_static_models"] == [{"model": "models/test.tik", "origin": [1, 2, 3], "angles": [10, 20, 30], "scale": 1.5}]
    assert report["native_side_equations"] == [[1, 0, 0, 5, 0, 1, 0, 7]]
    assert set(report["native_side_equation_indices"]) == {0}
    assert report["native_shaders"][0]["fence_mask"] == "textures/q3mapx/checker"
    assert len(report["native_terrain"]) == 2
    assert report["native_terrain"][0]["height_steps"] == list(range(81))
    assert report["native_terrain"][0]["origin"] == [-64, -128, 50]
    run(exe, [*base, "-convert", "-format", "obj", path], root, "mesh")
    obj = path.with_suffix(".obj").read_text()
    base_faces = sum(struct.unpack_from("<i", bsp.lump(13), i + 24)[0] // 3
                     for i in range(0, len(bsp.lump(13)), 104)
                     if struct.unpack_from("<i", bsp.lump(13), i + 8)[0] in (1, 3))
    assert sum(line.startswith("f ") for line in obj.splitlines()) == base_faces + 254
    assert "v -64.000000 50.000000 128.000000" in obj
    # The world terrain insertion precedes the fixture's brush entity surfaces.
    # OBJ must still export that entity's original geometry after index remapping.
    insertion = sum(struct.unpack_from("<ii", bsp.lump(7), 24))
    assert f"model0surf{insertion}" in obj and f"model0surf{insertion+1}" in obj
    assert "model1surf" in obj
    assert path.read_bytes() == data
    catalog = json.loads(subprocess.check_output([str(exe), "-games"], cwd=root))
    profile = next(p for p in catalog["profiles"] if p["id"] == "mohaa")
    assert profile["workflows"] == ["decompile"] and profile["native_write"] is False
    minimap = root / "keep.tga"; minimap.write_bytes(b"keep")
    p = subprocess.run([str(exe), *map(str, base), "-minimap", "-o", str(minimap), str(path)],
                       cwd=root, capture_output=True, timeout=15)
    assert p.returncode == 1 and b"native terrain would be omitted" in p.stdout and minimap.read_bytes() == b"keep"

    def rejected(changed, label):
        bad = root / "broken.bsp"; bad.write_bytes(pack(changed))
        output.write_text("protected output")
        p = subprocess.run([str(exe), *map(str, base), "-force", "-decompile", "-o", str(output), str(bad)],
                           cwd=root, capture_output=True, timeout=15)
        assert p.returncode == 1 and b"Invalid BSP" in p.stdout, (label, p.stdout)
        assert output.read_text() == "protected output"
    for lump, offset, fmt, value in ((22, 40, "<H", 65535), (22, 42, "<H", 2),
                                    (22, 44, "<h", 10), (22, 4, "<f", float("nan")),
                                    (23, 0, "<H", 3), (25, 156, "<i", 3), (25, 152, "<f", float("inf")),
                                    (26, 0, "<H", 2), (8, 52, "<i", 3), (8, 60, "<i", 2), (11, 8, "<i", 1)):
        changed = list(lumps); value_bytes = bytearray(changed[lump]); struct.pack_into(fmt, value_bytes, offset, value)
        changed[lump] = bytes(value_bytes); rejected(changed, (lump, offset))
    changed = list(lumps); changed[10] = b""; rejected(changed, "active fence without equations")
    changed = list(lumps); changed[22] = terrain() * 16385; rejected(changed, "terrain expansion budget")
    changed = list(lumps); changed[25] = b"x" * 128 + lumps[25][128:]; rejected(changed, "model string")
    changed = list(lumps); changed[0] = lumps[0][:76] + b"x" * 64 + lumps[0][140:]; rejected(changed, "fence string")
    # Retail non-fence briefing maps have zero equation references and no table.
    changed = list(lumps); changed[10] = b""
    shaders = bytearray(changed[0]); struct.pack_into("<I", shaders, 68, struct.unpack_from("<I", shaders, 68)[0] & ~0x2000)
    changed[0] = bytes(shaders); path.write_bytes(pack(changed))
    run(exe, [*base, "-decompile", "-o", output, path], root, "unused-equations")
    repaired = json.loads(Path(str(output) + ".recovery.json").read_text())
    assert repaired["normalized_unused_native_equations"] == len(lumps[11]) // 12
    assert set(repaired["native_side_equation_indices"]) == {-1}
    print("MOHAA native geometry, terrain OBJ/holes, static placements, fence metadata and malformed extensions passed")


if __name__ == "__main__":
    main()

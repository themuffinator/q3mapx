# SPDX-License-Identifier: GPL-3.0-or-later
"""Generated FAKK 12/42 geometry, extension metadata and FTX texture recovery."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from fixtures import create_fixture
from integration import Bsp, check_uvs, run


def native_file(bsp, version):
    shaders = b"".join(bsp.lump(1)[i:i+72] + struct.pack("<i", 24 + i // 72)
                       for i in range(0, len(bsp.lump(1)), 72))
    surfaces = b"".join(bsp.lump(13)[i:i+104] + struct.pack("<f", 12.5 + i // 104)
                        for i in range(0, len(bsp.lump(13)), 104))
    # Native shader and surface strides differ, and native directory ordering
    # includes a checksum plus three extension lumps absent from IBSP.
    lumps = [shaders, bsp.lump(2), bsp.lump(14), surfaces, bsp.lump(10), bsp.lump(11),
             bsp.lump(6), bsp.lump(5), bsp.lump(4), bsp.lump(3), bsp.lump(9), bsp.lump(8),
             bsp.lump(12), bsp.lump(7), bsp.lump(0), bsp.lump(16), bsp.lump(15),
             bytes(16), bytes(4), bytes(52)]
    result = bytearray(b"FAKK" + struct.pack("<iI", version, 0x91827364) + bytes(20 * 8))
    for index, lump in enumerate(lumps):
        result.extend(b"\0")  # native records are intentionally unaligned
        struct.pack_into("<ii", result, 12 + index * 8, len(result), len(lump))
        result.extend(lump)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    source = create_fixture(root / "q3")
    # Asymmetric image dimensions expose a missing-image fallback in UV recovery.
    width, height = 128, 32
    rgb = bytes([80, 120, 200]) * (width * height)
    tga = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, width, height, 24, 0x20) + rgb
    (root / "q3/baseq3/textures/q3mapx/checker.tga").write_bytes(tga)
    common = ["-fs_basepath", root / "q3", "-fs_homepath", root / "home", "-threads", 2]
    run(exe, ["-game", "quake3", *common, "-meta", source], root, "compile-reference")
    original = Bsp(source.with_suffix(".bsp"))
    catalog = json.loads(subprocess.check_output([str(exe), "-games"], cwd=root))
    for game, version, directory in (("fakk2", 12, "fakk"), ("alice", 42, "base")):
        profile = next(p for p in catalog["profiles"] if p["id"] == game)
        assert profile["native_write"] is False and set(profile["workflows"]) == {"decompile", "minimap"}
        folder = root / game
        create_fixture(folder, game_directory=directory)
        texture = folder / directory / "textures/q3mapx/checker"
        texture.with_suffix(".tga").unlink()  # this fixture's generated TGA only
        ftx = struct.pack("<3I", width, height, 1) + bytes([200, 120, 80, 255]) * (width * height)
        texture.with_suffix(".ftx").write_bytes(ftx)
        data = native_file(original, version)
        path = folder / "native.bsp"
        path.write_bytes(data)
        digest = hashlib.sha256(data).digest()
        base = ["-game", game, "-fs_basepath", folder, "-fs_homepath", folder / "home", "-threads", 2]
        run(exe, [*base, "-info", path], folder, "info")
        output = folder / "recovered.map"
        run(exe, [*base, "-decompile", "-o", output, path], folder, "recover")
        report = json.loads(Path(str(output) + ".recovery.json").read_text())
        assert report["game"] == game and report["native_write_supported"] is False
        assert report["brushes"] == 8 and report["patches"] == 1 and report["skipped_brushes"] == 0
        assert {e["feature"]: e["bytes"] for e in report["native_losses"]} == {
            "entity_lights": 16, "entity_light_visibility": 4, "light_definitions": 52}
        assert [s["subdivisions"] for s in report["native_shaders"]] == list(range(24, 24 + original.summary()["shaders"]))
        assert report["native_surface_subdivisions"] == [12.5 + i for i in range(original.summary()["surfaces"])]
        for index, shader in enumerate(report["native_shaders"]):
            flags, contents = struct.unpack_from("<II", original.lump(1), index * 72 + 64)
            assert shader["contents"] == contents and shader["surface_flags"] == flags
        assert "Couldn't find image for shader textures/q3mapx/stone" not in (folder / "recover.log").read_text()
        # Cross-format geometry/UV check: use the original Quake III asset fixture
        # to recompile the recovered standard MAP, not a disabled native writer.
        run(exe, ["-game", "quake3", *common, "-meta", output], folder, "roundtrip")
        check_uvs(original, Bsp(output.with_suffix(".bsp")))
        run(exe, [*base, "-minimap", "-backend", "cpu", "-size", 32, "-samples", 1,
                  "-o", folder / "minimap.tga", path], folder, "minimap")
        assert (folder / "minimap.tga").stat().st_size > 18
        # No unsupported writer may create even an auxiliary output, with -force
        # or an already-existing destination. Both conversion directions fail.
        for operation in (["-meta"], ["-vis"], ["-light"], ["-scale", "1"], ["-shift", "0", "0", "0"],
                          ["-import"], ["-json"], ["-convert", "-format", "quake3"]):
            before = {str(p.relative_to(folder)) for p in folder.rglob("*") if p.is_file()}
            p = subprocess.run([str(exe), *map(str, base), "-force", *operation, str(path)], cwd=folder,
                               capture_output=True, timeout=15)
            assert p.returncode == 1 and b"recovery" in p.stdout, p.stdout
            assert before == {str(p.relative_to(folder)) for p in folder.rglob("*") if p.is_file()}
            assert hashlib.sha256(path.read_bytes()).digest() == digest
        p = subprocess.run([str(exe), "-game", "quake3", *map(str, common), "-convert", "-format", game,
                            str(source.with_suffix(".bsp"))], cwd=folder, capture_output=True, timeout=15)
        assert p.returncode == 1 and b"recovery-only" in p.stdout
        # Exact native layouts and full geometry validation remain strict even
        # when -force is supplied. Existing destination content stays untouched.
        bad_cases = []
        wrong_version = bytearray(data); struct.pack_into("<i", wrong_version, 4, 42 if version == 12 else 12)
        bad_cases.append(wrong_version)
        bad_cases.append(data[:171])
        wrong_length = bytearray(data); struct.pack_into("<i", wrong_length, 12 + 3 * 8 + 4, len(original.lump(13)) // 104 * 108 - 1)
        bad_cases.append(wrong_length)
        bad_range = bytearray(data); struct.pack_into("<i", bad_range, 12, -1); bad_cases.append(bad_range)
        bad_vertex = bytearray(data)
        vertex_offset = struct.unpack_from("<i", data, 12 + 4 * 8)[0]
        struct.pack_into("<f", bad_vertex, vertex_offset, float("nan")); bad_cases.append(bad_vertex)
        for index, bad in enumerate(bad_cases):
            broken = folder / "broken.bsp"; broken.write_bytes(bad)
            output.write_text("keep this destination")
            p = subprocess.run([str(exe), *map(str, base), "-force", "-decompile", "-o", str(output), str(broken)],
                               cwd=folder, capture_output=True, timeout=15)
            assert p.returncode == 1 and b"Invalid BSP" in p.stdout, (index, p.stdout)
            assert output.read_text() == "keep this destination"
        # Truncated FTX is a diagnosed missing texture, never an unchecked read.
        texture.with_suffix(".ftx").write_bytes(ftx[:15])
        run(exe, [*base, "-decompile", "-o", output, path], folder, "bad-ftx")
        assert "Truncated FTX pixel data" in (folder / "bad-ftx.log").read_text()
        texture.with_suffix(".ftx").write_bytes(ftx)
        print(game, "native geometry/metadata, FTX UV roundtrip, minimap and write protection passed", flush=True)


if __name__ == "__main__":
    main()

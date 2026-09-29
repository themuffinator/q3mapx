# SPDX-License-Identifier: GPL-3.0-or-later
"""Native 43/44/45 records, inferred face materials and tree-owned model recovery."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from fixtures import create_fixture
from integration import Bsp, check_uvs, run


def records(data, stride):
    return [data[i:i+stride] for i in range(0, len(data), stride)]


def pack_file(version, lumps):
    data = bytearray(b"IBSP" + struct.pack("<i", version) + bytes(8*len(lumps)))
    for index, payload in enumerate(lumps):
        data.extend(b"\0")  # exercise unaligned records
        struct.pack_into("<ii", data, 8+index*8, len(data), len(payload))
        data.extend(payload)
    return data


def native_lumps(bsp, version):
    planes = b"".join(p + struct.pack("<i", 3) for p in records(bsp.lump(2), 16))
    source_models = records(bsp.lump(7), 40)
    if version == 45:
        lumps = [bsp.lump(i) for i in range(17)]
        lumps[2] = planes
        lumps[7] = b"".join(m[:24] + struct.pack("<3fi", 11, 22, 33, 0) + m[24:] for m in source_models)
        lumps[12] = b"textures/q3mapx/fog\0".ljust(64, b"\0") + struct.pack("<i", 0)
        return lumps

    # Split rendered polygons into triangle fans; patches keep their controls.
    # 44 alternates indexed triangles with fans to exercise both native types.
    source_vertices = records(bsp.lump(10), 44)
    verts, surfaces, mapping = [], [], []
    for surface in records(bsp.lump(13), 104):
        shader, fog, kind, first, count, first_index, num_indices = struct.unpack_from("<7i", surface)
        name = bsp.lump(1)[shader*72:shader*72+64]
        polygons = [list(range(count))] if kind == 2 else [list(struct.unpack_from("<3i", bsp.lump(11), 4*i)) for i in range(first_index, first_index+num_indices, 3)]
        ids = []
        for polygon in polygons:
            first_vert = len(verts)
            verts.extend(source_vertices[first+v] for v in polygon)
            indices = struct.pack("<2i", 0, 3 if kind != 2 and len(surfaces) % 2 else 0) if version == 44 else b""
            patch = surface[96:104] if kind == 2 else bytes(8)
            native = name + struct.pack("<4i", fog, -1, first_vert, len(polygon)) + indices + patch + surface[28:96]
            assert len(native) == (156 if version == 43 else 164)
            ids.append(len(surfaces)); surfaces.append(native)
        mapping.append(ids)
    # Source order deliberately puts submodel geometry first. Native header
    # ranges are stale; only head-node leaf ownership can recover the door.
    surfaces.reverse()
    mapping = [[len(surfaces)-1-i for i in ids] for ids in mapping]
    source_brushes = records(bsp.lump(8), 12)
    brush_map = lambda index: len(source_brushes)-1-index
    brushes = []
    for brush in reversed(source_brushes):
        shader = struct.unpack_from("<i", brush, 8)[0]
        contents = struct.unpack_from("<i", bsp.lump(1), shader*72+68)[0]
        brushes.append(brush[:8]+struct.pack("<i", contents))
    sides = []
    for side in records(bsp.lump(9), 8):
        shader = struct.unpack_from("<i", side, 4)[0]
        flags = struct.unpack_from("<i", bsp.lump(1), shader*72+64)[0]
        sides.append(side[:4]+struct.pack("<i", flags))
    leafs, leaf_surfaces, leaf_brushes = [], [], []

    def leaf(prefix, surface_ids, brush_ids):
        first_s, first_b = len(leaf_surfaces), len(leaf_brushes)
        leaf_surfaces.extend(surface_ids); leaf_brushes.extend(brush_ids)
        leafs.append(prefix + struct.pack("<4i", first_s, len(surface_ids), first_b, len(brush_ids)))

    for record in records(bsp.lump(4), 48):
        fs, ns, fb, nb = struct.unpack_from("<4i", record, 32)
        old_s = [struct.unpack_from("<i", bsp.lump(5), i*4)[0] for i in range(fs, fs+ns)]
        old_b = [struct.unpack_from("<i", bsp.lump(6), i*4)[0] for i in range(fb, fb+nb)]
        leaf(record[:32], [s for i in old_s for s in mapping[i]], [brush_map(i) for i in old_b])
    models = []
    for index, model in enumerate(source_models):
        fs, ns, fb, nb = struct.unpack_from("<4i", model, 24)
        head = 0
        if index:
            head = -1-len(leafs)
            leaf(struct.pack("<8i", -1, 0, -1024, -1024, -1024, 1024, 1024, 1024),
                 [s for i in range(fs, fs+ns) for s in mapping[i]], [brush_map(i) for i in range(fb, fb+nb)])
        models.append(model[:24] + struct.pack("<3f3i", 11, 22, 33, head, 0x7fffffff, -77))
    fog = b"textures/q3mapx/fog\0".ljust(64, b"\0") + struct.pack("<i", brush_map(0))
    lumps = [bsp.lump(0), planes, bsp.lump(3), b"".join(leafs),
             struct.pack(f"<{len(leaf_surfaces)}i", *leaf_surfaces), struct.pack(f"<{len(leaf_brushes)}i", *leaf_brushes),
             b"".join(models), b"".join(brushes), b"".join(sides), bsp.lump(14), bsp.lump(16),
             b"".join(verts), b"".join(surfaces), fog]
    if version == 44:
        lumps.append(struct.pack("<3i", 0, 1, 2))
    return lumps


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    source = create_fixture(root / "q3")
    common = ["-game", "quake3", "-fs_basepath", root / "q3", "-fs_homepath", root / "home", "-threads", 2]
    run(exe, [*common, "-meta", source], root, "reference")
    original = Bsp(source.with_suffix(".bsp"))
    catalog = json.loads(subprocess.check_output([str(exe), "-games"], cwd=root))
    for version, game in ((43, "q3-ihv"), (44, "q3test44"), (45, "q3test45")):
        profile = next(p for p in catalog["profiles"] if p["id"] == game)
        assert not profile["native_write"] and set(profile["workflows"]) == {"decompile", "minimap", "obj", "ase"}
        folder = root / game
        create_fixture(folder, game_directory="baseq3" if version == 43 else "demoq3")
        base = ["-game", game, "-fs_basepath", folder, "-fs_homepath", folder / "home", "-threads", 2]
        lumps = native_lumps(original, version)
        data = pack_file(version, lumps)
        path = folder / "native.bsp"; path.write_bytes(data)
        digest = hashlib.sha256(data).digest()
        output = folder / "recovered.map"
        run(exe, [*base, "-decompile", "-o", output, path], folder, "recover")
        report = json.loads(Path(str(output)+".recovery.json").read_text())
        assert report["brushes"] == 8 and report["patches"] == 1 and report["skipped_brushes"] == 0, report
        assert report["native_models"][1]["origin"] == [11, 22, 33]
        if version != 45:
            assert report["native_models"][1]["declared_surface_count"] == -77
            assert report["native_brush_source_indices"] == [1, 2, 3, 4, 5, 6, 7, 0]
            assert report["inferred_material_faces"] == 17, report  # 6 room + 5 column + 6 door faces
            assert report["native_brush_contents"] == [struct.unpack_from("<I", lumps[7], i+8)[0] for i in range(0,len(lumps[7]),12)]
            assert len(report["native_surface_source_indices"]) == len(lumps[12])//(156 if version==43 else 164)
        # UV comparison and surviving door model exercise actual material
        # inference and tree ownership, not only a successful parse.
        run(exe, [*common, "-meta", output], folder, "roundtrip")
        rebuilt = Bsp(output.with_suffix(".bsp"))
        assert rebuilt.summary()["models"] == 2 and rebuilt.summary()["brushes"] == 8
        check_uvs(original, rebuilt)
        run(exe, [*base, "-convert", "-format", "obj", path], folder, "mesh")
        assert sum(line.startswith("f ") for line in path.with_suffix(".obj").read_text().splitlines()) > 30
        run(exe, [*base, "-minimap", "-backend", "cpu", "-size", 32, "-samples", 1, "-o", folder / "minimap.tga", path], folder, "minimap")
        assert (folder / "minimap.tga").stat().st_size > 18
        p = subprocess.run([str(exe), *map(str, base), "-force", "-scale", "1", str(path)], cwd=folder, capture_output=True, timeout=15)
        assert p.returncode == 1 and b"recovery" in p.stdout
        assert hashlib.sha256(path.read_bytes()).digest() == digest

        bad_cases = []
        def bad(lump, offset, fmt, value):
            damaged = list(lumps); payload = bytearray(damaged[lump])
            struct.pack_into(fmt, payload, offset, value); damaged[lump] = payload
            bad_cases.append(pack_file(version, damaged))
        model_slot = 7 if version == 45 else 6
        bad(model_slot, 24, "<f", float("nan"))
        bad(model_slot, 36, "<i", -2147483648)
        bad(model_slot, 36, "<i", 2147483647)
        bad(3 if version == 45 else 2, 4, "<i", 0)  # cycle at root
        bad(10 if version == 45 else 11, 0, "<f", float("inf"))
        if version != 45:
            bad(12, 72, "<i", -1)
            bad(12, 80 if version == 43 else 88, "<i", 4)  # malformed patch
            damaged = list(lumps); damaged[12] = b"x"*64 + damaged[12][64:]
            bad_cases.append(pack_file(version, damaged))
            damaged = list(lumps); damaged[6] += damaged[6][48:96]  # overlapping model ownership
            bad_cases.append(pack_file(version, damaged))
        if version == 44:
            bad(14, 0, "<i", 3)  # relative index beyond triangle vertices
        wrong_version = bytearray(data); struct.pack_into("<i", wrong_version, 4, 46); bad_cases.append(wrong_version)
        wrong_stride = bytearray(data); struct.pack_into("<i", wrong_stride, 12+model_slot*8, len(lumps[model_slot])-1); bad_cases.append(wrong_stride)
        for index, damaged in enumerate(bad_cases):
            broken = folder / "broken.bsp"; broken.write_bytes(damaged)
            output.write_text("keep existing MAP")
            p = subprocess.run([str(exe), *map(str, base), "-force", "-decompile", "-o", str(output), str(broken)], cwd=folder, capture_output=True, timeout=20)
            assert p.returncode == 1 and b"Invalid BSP" in p.stdout, (version, index, p.stdout)
            assert output.read_text() == "keep existing MAP"
        print(game, "native records, model ownership, material/UV roundtrip, OBJ, minimap and malformed inputs passed", flush=True)


if __name__ == "__main__":
    main()

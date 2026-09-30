"""Known-source detail/structure and spatial VIS round trips. GPL-3.0-or-later.

These tests label original generated MAPs, not the recovered leaf heuristic.
They establish fixture fidelity, not universal source-classification accuracy.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from fixtures import box, create_fixture
from integration import Bsp, run
from native_early import pack_file

DETAIL = 134217728


def create_case(root, mode):
    source = create_fixture(root, dense=True, grid=3, patch=False)
    text = source.read_text(encoding="utf-8")
    groups, labels = [], {}
    for i, (x, y) in enumerate((x, y) for y in (-128, 0, 128) for x in (-128, 0, 128)):
        lo, hi = (x-20, y-20, 0), (x+20, y+20, 80+(x+y) % 112)
        original = box(lo, hi)
        assert text.count(original) == 1
        detail = mode == "detail" or (mode == "mixed-group" and i % 2 == 0)
        authored = original
        if detail or mode == "structural-override":
            authored = authored.replace(" 0 0 0\n", f" {DETAIL} 0 0\n")
        if mode in ("structural-override", "translucent-structural"):
            authored = authored.replace("q3mapx/stone", "q3mapx/structural")
        if mode == "mixed-opacity" and i % 2 == 0:
            # Only the last side is translucent: using the brush's first shader
            # instead of the exported sides would misclassify its insertion order.
            authored = authored.replace("q3mapx/stone", "q3mapx/opaque_structural", 5)
            authored = authored.replace("q3mapx/stone", "q3mapx/structural")
        if mode == "mixed-group":
            groups.append(authored)
            authored = ""
        text = text.replace(original, authored)
        labels[(*lo, *hi)] = detail
    if groups:
        text += '{\n"classname" "func_group"\n"name" "mixed authoring assembly"\n' + "".join(groups) + '}\n'
    if mode in ("structural-override", "translucent-structural", "mixed-opacity"):
        shader = source.parent.parent / "scripts/q3mapx_tests.shader"
        with shader.open("a", encoding="utf-8") as out:
            out.write("\ntextures/q3mapx/structural\n{\nsurfaceparm structural\nsurfaceparm trans\n"
                      "{ map textures/q3mapx/checker.tga }\n}\n"
                      "\ntextures/q3mapx/opaque_structural\n{\nsurfaceparm structural\n"
                      "{ map textures/q3mapx/checker.tga }\n}\n")
    source.write_text(text, encoding="utf-8")
    return source, labels


def brush_labels(bsp, labels):
    planes = list(struct.iter_unpack("<4f", bsp.lump(2)))
    sides = list(struct.iter_unpack("<2i", bsp.lump(9)))
    brushes = list(struct.iter_unpack("<3i", bsp.lump(8)))
    first, count = struct.unpack_from("<2i", bsp.lump(7), 32)
    result, matched = {}, set()
    for index in range(first, first+count):
        start, size, _ = brushes[index]
        lo, hi = [-float("inf")]*3, [float("inf")]*3
        for plane, _ in sides[start:start+size]:
            nx, ny, nz, distance = planes[plane]
            normal = (nx, ny, nz)
            for axis in range(3):
                if normal[axis] and not normal[(axis+1) % 3] and not normal[(axis+2) % 3]:
                    bound = distance/normal[axis]
                    if normal[axis] > 0:
                        hi[axis] = min(hi[axis], bound)
                    else:
                        lo[axis] = max(lo[axis], bound)
        key = (*lo, *hi)
        if key in labels:
            matched.add(key)
        # The six enclosing room brushes are structural source labels too.
        result[index] = labels.get(key, False)
    assert len(result) == 15 and matched == set(labels)
    return result


def exported_flags(path, world_only=True):
    entity, brush, flags = -1, -1, {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"// entity (\d+)", line)
        if match:
            entity = int(match[1])
        match = re.fullmatch(r"\s*// brush (-?\d+)", line)
        if match:
            brush = int(match[1])
        match = re.search(r" (\d+) 0 0$", line)
        if (entity == 0 or not world_only) and brush >= 0 and match:
            flags.setdefault(brush if world_only else (entity, brush), set()).add(bool(int(match[1]) & DETAIL))
    return flags


def spatial_signature(bsp, points):
    planes = list(struct.iter_unpack("<4f", bsp.lump(2)))
    nodes = list(struct.iter_unpack("<9i", bsp.lump(3)))
    leaves = list(struct.iter_unpack("<12i", bsp.lump(4)))
    clusters = []
    for point in points:
        node = 0
        for _ in range(1025):
            if node < 0:
                clusters.append(leaves[-1-node][0])
                break
            plane = planes[nodes[node][0]]
            distance = sum(a*b for a, b in zip(point, plane[:3])) - plane[3]
            node = nodes[node][1 if distance >= 0 else 2]
        else:
            raise AssertionError("BSP point traversal exceeded validated depth")
    vis = bsp.lump(16)
    count, stride = struct.unpack_from("<2i", vis)
    assert all(c < count for c in clusters)
    # Compare spatial relationships, never assume cluster IDs survive rebuilding.
    partition = bytes(a == b for a in clusters for b in clusters)
    pvs = bytes(False if a < 0 or b < 0 else bool(vis[8+a*stride+b//8] & (1 << (b % 8)))
                for a in clusters for b in clusters)
    return bytes(c >= 0 for c in clusters), partition, pvs


def geometry_signature(bsp, *, ordered=False):
    """Exact authored boxes, contents/materials and brush-model geometry in this corpus."""
    planes = list(struct.iter_unpack("<4f", bsp.lump(2)))
    sides = list(struct.iter_unpack("<2i", bsp.lump(9)))
    brushes = list(struct.iter_unpack("<3i", bsp.lump(8)))
    shaders = list(struct.iter_unpack("<64s2i", bsp.lump(1)))
    result = []
    for offset in range(0, len(bsp.lump(7)), 40):
        first, count = struct.unpack_from("<2i", bsp.lump(7), offset+32)
        solids = []
        for start, size, shader in brushes[first:first+count]:
            faces = tuple(sorted((planes[plane], shaders[material]) for plane, material in sides[start:start+size]))
            solids.append((faces, shaders[shader]))
        result.append(tuple(solids if ordered else sorted(solids)))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(strict=True), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    checks = []
    # Includes brush interiors, free space, enclosing solids and outside space;
    # coordinates avoid the known face planes and game entity locations.
    points = [(x, y, z) for x in (-281, -261, -199, -127, -63, 1, 65, 129, 201, 263, 283)
              for y in (-281, -199, -127, -63, 1, 65, 129, 201, 283) for z in (-7, 31, 117, 223, 263, 283)]
    for mode in ("detail", "structural", "mixed-group", "structural-override"):
        directory = root / mode
        source, labels = create_case(directory, mode)
        source_bytes = source.read_bytes()
        base = ["-game", "quake3", "-fs_basepath", directory, "-fs_homepath", root/"home", "-threads", 1]
        run(exe, [*base, "-meta", source], root, mode+"-source-bsp")
        run(exe, [*base, "-vis", "-reproducible", "-saveprt", source], root, mode+"-source-vis")
        path = source.with_suffix(".bsp")
        original = Bsp(path)
        expected = brush_labels(original, labels)
        signature = spatial_signature(original, points)
        geometry = geometry_signature(original)
        source_counts = original.summary()
        full_rebuilds = {}
        for fast in (False, True):
            for fmt in ("map", "map_bp", "map_220"):
                label = f'{mode}-{"fast" if fast else "full"}-{fmt}'
                output = directory / f'{label}.map'
                options = ["-fast"] if fast else []
                run(exe, [*base, "-decompile", *options, "-format", fmt, "-o", output, path], root, label)
                actual = exported_flags(output)
                assert actual == {b: {v} for b, v in expected.items()}, (label, expected, actual)
                report = json.loads(Path(str(output)+".recovery.json").read_text())
                policy = report["detail_classification"]
                assert policy["method"] == "nonopaque_leaf_reference_heuristic" and not policy["author_classification_proven"]
                all_flags = exported_flags(output, world_only=False)
                flagged = sum(values == {True} for values in all_flags.values())
                assert len(all_flags) == report["brushes"]
                assert policy["exported_brushes_with_detail_flag"] == flagged
                assert policy["exported_brushes_without_detail_flag"] + flagged == report["brushes"]
                assert report["faces"] == report["matched_uv_faces"] + report["fallback_uv_faces"]
                if fast:
                    assert report["faces"] == report["fallback_uv_faces"] > 0 and not report["matched_uv_faces"]
                run(exe, [*base, "-meta", output], root, label+"-bsp")
                assert not output.with_suffix(".lin").exists(), label+" leaked"
                run(exe, [*base, "-vis", "-reproducible", "-saveprt", output], root, label+"-vis")
                rebuilt = Bsp(output.with_suffix(".bsp"))
                counts = rebuilt.summary()
                for field in ("models", "brushes"):
                    assert counts[field] == source_counts[field], (label, field, source_counts[field], counts[field])
                observed = spatial_signature(rebuilt, points)
                assert geometry_signature(rebuilt) == geometry, label+" changed brush geometry, contents or materials"
                assert observed[0] == signature[0], label+" changed sampled opaque/nonopaque space"
                if not fast:
                    full_rebuilds[fmt] = (counts, observed)
                else:
                    reference_counts, reference = full_rebuilds[fmt]
                    for field in ("nodes", "leaves"):
                        assert counts[field] == reference_counts[field], (label, field, "differs from full recovery")
                    assert observed == reference, label+" changed sampled partitions or visibility relative to full recovery"
                assert b'"targetname" "test_door"' in rebuilt.lump(0)
                checks.append({"case": mode, "fast": fast, "format": fmt, "source_sha256": hashlib.sha256(original.data).hexdigest(),
                               "labelled_world_brushes": len(expected), "expected_detail_brushes": sum(expected.values()),
                               "source_nodes": source_counts["nodes"], "nodes": counts["nodes"], "leaves": counts["leaves"],
                               "samples": len(points), "sample_pairs": len(points)**2, "geometry_materials_contents_match": True,
                               "opaque_space_matches_source": True,
                               "partition_pair_differences_from_source": sum(a != b for a, b in zip(signature[1], observed[1])),
                               "pvs_pairs_added_to_source": sum(not a and b for a, b in zip(signature[2], observed[2])),
                               "pvs_pairs_removed_from_source": sum(a and not b for a, b in zip(signature[2], observed[2])),
                               "matches_full_recovery": True if fast else None})
        # VIS bytes are optional input evidence, not a substitute for compiled leaf membership.
        payloads = [original.lump(i) if i != 16 else b"" for i in range(17)]
        missing = directory / "without-vis.bsp"; missing.write_bytes(pack_file(46, payloads))
        output = directory / "without-vis.map"
        run(exe, [*base, "-decompile", "-fast", "-o", output, missing], root, mode+"-without-vis")
        assert exported_flags(output) == {b: {v} for b, v in expected.items()}
        assert source.read_bytes() == source_bytes and path.read_bytes() == original.data
    # Same recovered bytes with a different job count; this is not a classifier confidence claim.
    directory = root / "detail"; output = directory / "workers4.map"
    base = ["-game", "quake3", "-fs_basepath", directory, "-fs_homepath", root/"home", "-threads", 4]
    run(exe, [*base, "-decompile", "-fast", "-o", output, directory/"baseq3/maps/fixture.bsp"], root, "workers4")
    assert output.read_bytes() == (directory / "detail-fast-map_220.map").read_bytes()
    (root / "validation.json").write_text(json.dumps({"schema_version": 1, "round_trips": checks,
        "missing_vis_cases": 4, "worker_parity": [1, 4], "sources_preserved": True,
        "scope": "Generated Quake III source labels and finite spatial samples; not a proof of universal inference accuracy"}, indent=2)+"\n")
    print("24 known-source round trips preserve world detail labels and brush geometry; fast/full spatial parity, absent VIS and worker parity passed")


if __name__ == "__main__":
    main()

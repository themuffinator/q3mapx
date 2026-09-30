"""Known-source assembly inference and rebuild/lighting controls. GPL-3.0-or-later."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from fixtures import box, create_fixture
from integration import Bsp, check_uvs, run
from native_early import pack_file
from recovery_classification import geometry_signature, spatial_signature


def entity_blocks(text):
    depth, current, result = 0, [], []
    for line in text.splitlines(keepends=True):
        if line.strip() == "{":
            depth += 1
        if depth:
            current.append(line)
        if line.strip() == "}":
            depth -= 1
            if depth == 0:
                result.append("".join(current))
                current = []
    assert depth == 0
    return result


def fixture(root, mode, patch=True):
    source = create_fixture(root, patch=patch)
    blocks = entity_blocks(source.read_text())
    blocks[0] = blocks[0].replace(box((-48, -48, 0), (48, 48, 96)), "")
    parameters = ('"_ambient" "8"\n"_color" "0.75 0.5 1"\n"_samplesize" "16"\n'
                  '"_lightmapscale" "1.5"\n"_castShadows" "1"\n"_receiveShadows" "1"\n') if mode == "world-params" else ""
    blocks[0] = blocks[0].replace('"classname" "worldspawn"\n', '"classname" "worldspawn"\n'+parameters)
    labels, groups, pieces = {}, [], []
    for name, parts in (
            ("arch", [((-160, -32, 0), (-120, 32, 64), True), ((-120, -32, 0), (-80, 32, 64), False)]),
            ("assembly", [((0, 64, 0), (40, 96, 48), True),
                          (((56 if mode == "disconnected" else 40), 64, 0), ((96 if mode == "disconnected" else 80), 96, 48), True)])):
        brushes = []
        for i, (lo, hi, detail) in enumerate(parts):
            brush = box(lo, hi)
            label_hi = hi
            if mode == "oblique" and name == "arch":
                def sloped(match):
                    x, y, z = map(float, match.groups())
                    if z == hi[2]:
                        z += (x+160)/4
                    return f"( {x:g} {y:g} {z:g} )"
                brush = re.sub(r"\( ([-\d.]+) ([-\d.]+) ([-\d.]+) \)", sloped, brush)
                label_hi = (*hi[:2], hi[2]+(hi[0]+160)/4)
            if detail:
                brush = brush.replace(" 0 0 0\n", " 134217728 0 0\n")
            if mode == "all-translucent" or (mode == "mixed-opacity" and i == 1):
                # The last face alone makes this brush translucent; other
                # coplanar stone faces still join its opaque partner's surfaces.
                at = brush.rfind("q3mapx/stone")
                brush = brush[:at]+brush[at:].replace("q3mapx/stone", "q3mapx/glass", 1)
            brushes.append(brush)
            labels[(*lo, *label_hi)] = name
        pieces += brushes
        groups.append('{\n"classname" "func_group"\n"name" "'+name+'"\n'+parameters+"".join(brushes)+'}\n')
    if mode == "flat-ambiguous":
        blocks[0] = blocks[0][:-2]+"".join(pieces)+'}\n'
    else:
        blocks += groups
    source.write_text("".join(blocks), encoding="utf-8")
    shader = source.parent.parent/"scripts/q3mapx_tests.shader"
    with shader.open("a", encoding="utf-8") as stream:
        stream.write("\ntextures/q3mapx/glass\n{\nsurfaceparm trans\n{ map textures/q3mapx/checker.tga blendFunc blend }\n}\n")
    return source, labels


def brush_names(bsp, labels):
    planes = list(struct.iter_unpack("<4f", bsp.lump(2)))
    sides = list(struct.iter_unpack("<2i", bsp.lump(9)))
    brushes = list(struct.iter_unpack("<3i", bsp.lump(8)))
    first, count = struct.unpack_from("<2i", bsp.lump(7), 32)
    names = {}
    for index in range(first, first+count):
        start, size, _ = brushes[index]
        lo, hi = [-float("inf")]*3, [float("inf")]*3
        for plane, _ in sides[start:start+size]:
            normal, distance = planes[plane][:3], planes[plane][3]
            for axis in range(3):
                if normal[axis] and not normal[(axis+1)%3] and not normal[(axis+2)%3]:
                    if normal[axis] > 0:
                        hi[axis] = min(hi[axis], distance/normal[axis])
                    else:
                        lo[axis] = max(lo[axis], distance/normal[axis])
        if (*lo, *hi) in labels:
            names[index] = labels[(*lo, *hi)]
    assert len(names) == 4
    return names


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(strict=True), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    checks, lights, precision = [], [], []
    points = [(x, y, z) for x in (-270, -250, -155, -125, -110, -85, 1, 32, 65, 85, 125, 250, 265)
              for y in (-270, -250, -31, 1, 31, 65, 85, 125, 250, 265) for z in (-7, 31, 63, 95, 263)]
    base = []

    def compile_map(path, label, bake=False):
        run(exe, [*base, "-meta", "-keeplights", path], root, label+"-bsp")
        assert not path.with_suffix(".lin").exists(), label+" leaked"
        run(exe, [*base, "-vis", "-saveprt", "-reproducible", path], root, label+"-vis")
        if bake:
            run(exe, [*base, "-light", "-fast", "-samples", 2, path], root, label+"-light")
        return Bsp(path.with_suffix(".bsp"))

    def recover(path, label, options=(), threads=1):
        output = root/(label+".map")
        before = path.read_bytes()
        run(exe, [*base, "-threads", threads, "-decompile", "-group-policy", "surfaces",
                  "-o", output, *options, path], root, label)
        assert path.read_bytes() == before
        return output, json.loads(Path(str(output)+".recovery.json").read_text())

    for mode in ("connected", "oblique", "mixed-opacity", "all-translucent", "world-params", "flat-ambiguous", "disconnected"):
        folder = root/mode
        source, labels = fixture(folder, mode)
        base = ["-game", "quake3", "-fs_basepath", folder, "-fs_homepath", root/"home", "-threads", 1]
        bake = mode in ("connected", "mixed-opacity", "world-params")
        original = compile_map(source, mode+"-source", bake)
        names = brush_names(original, labels)
        expected = {frozenset(i for i, name in names.items() if name == group) for group in ("arch", "assembly")}
        geometry = geometry_signature(original, ordered=True)
        spatial = spatial_signature(original, points)
        for fast in (False, True):
            for fmt in ("map", "map_bp", "map_220"):
                label = f'{mode}-{"fast" if fast else "full"}-{fmt}'
                output, report = recover(source.with_suffix(".bsp"), label,
                                         ["-format", fmt, *(["-fast"] if fast else [])])
                groups = report["group_inference"]
                assert groups["exported_groups"] == (0 if mode == "disconnected" else 2), (label, groups)
                assert not groups["author_grouping_proven"] and not groups["original_group_parameters_recovered"]
                assert not groups["bsp_rebuild_validated"] and not groups["original_shader_assets_verified"]
                assert groups["compile_parameter_basis"] == "copied_from_recovered_worldspawn"
                assert report["brush_order"]["policy"] == "rebuild"
                exported = [g for g in groups["groups"] if g["exported"]]
                assert {frozenset(g["brush_indices"]) for g in exported} == (set() if mode == "disconnected" else expected)
                if mode == "connected":
                    assert sorted(g["members_with_detail_flag"] for g in exported) == [1, 2]
                blocks = entity_blocks(output.read_text())
                group_blocks = [b for b in blocks if '"classname" "func_group"' in b]
                assert len(group_blocks) == len(exported)
                assert report["entities"] == len(blocks) == groups["source_entities"]+len(exported)
                assert 'patchDef2' in blocks[0] and all('patchDef2' not in b for b in group_blocks)
                assert all('"origin"' not in b and '"targetname"' not in b for b in group_blocks)
                all_members = [int(i) for block in group_blocks for i in re.findall(r"// brush (\d+)", block)]
                assert len(all_members) == len(set(all_members)) == 2*len(exported)
                assert set(all_members) <= names.keys()
                if mode == "world-params":
                    assert set(groups["copied_compile_keys"]) == {"_ambient", "_color", "_samplesize", "_lightmapscale", "_castShadows", "_receiveShadows"}
                    assert all('"_lightmapscale" "1.5"' in b and '"_ambient" "8"' in b for b in group_blocks)
                rebuild_bake = bake and not fast and fmt == "map_220"
                rebuilt = compile_map(output, label, rebuild_bake)
                assert geometry_signature(rebuilt, ordered=True) == geometry, label+" changed brush geometry/materials/contents/order"
                assert spatial_signature(rebuilt, points) == spatial, label+" changed partitions/PVS"
                assert rebuilt.summary()["nodes"] == original.summary()["nodes"]
                assert rebuilt.lump(0).split(b'}', 1)[1] == original.lump(0).split(b'}', 1)[1], label+" changed surviving entities"
                if not fast:
                    check_uvs(original, rebuilt)
                if rebuild_bake:
                    assert original.lump(14) and max(original.lump(14)) > 0
                    assert rebuilt.lump(14) == original.lump(14), label+" changed baked lightmaps"
                    assert rebuilt.lump(15) == original.lump(15), label+" changed lightgrid"
                    lights.append({"case": mode, "lightmaps_sha256": hashlib.sha256(original.lump(14)).hexdigest(),
                                   "lightgrid_sha256": hashlib.sha256(original.lump(15)).hexdigest()})
                checks.append({"case": mode, "fast": fast, "format": fmt, "groups_exported": len(exported),
                               "source_grouping_known": mode != "flat-ambiguous", "group_members_match_labeled_assemblies": mode != "disconnected",
                               "brush_geometry_materials_contents_order_match": True, "sampled_partitions_pvs_match": True})
        # Detail policy changes may not silently partition grouping proposals.
        output, cells = recover(source.with_suffix(".bsp"), mode+"-cells", ["-detail-policy", "cells"])
        assert cells["group_inference"] == report["group_inference"]
        parallel, four = recover(source.with_suffix(".bsp"), mode+"-cells-4", ["-detail-policy", "cells"], threads=4)
        assert output.read_bytes() == parallel.read_bytes() and cells["group_inference"] == four["group_inference"]
        if mode == "connected":
            saved = folder, source.with_suffix(".bsp"), original

    # The oblique fast-writer fix is independent of grouping. Rotate the whole
    # generated scene through every remaining dominant-axis/sign combination,
    # including entity origins, and require exact stored brush planes on rebuild.
    rotations = {
        "positive-x": lambda x, y, z: (z, y, -x),
        "negative-x": lambda x, y, z: (-z, y, x),
        "positive-y": lambda x, y, z: (x, z, -y),
        "negative-y": lambda x, y, z: (x, -z, y),
        "negative-z": lambda x, y, z: (-x, y, -z),
    }
    for label, rotate in rotations.items():
        directory = root/("precision-"+label)
        source, _ = fixture(directory, "oblique", patch=False)
        text = source.read_text()
        text = re.sub(r"\( ([-\d.]+) ([-\d.]+) ([-\d.]+) \)",
                      lambda m: "( %g %g %g )" % rotate(*map(float, m.groups())), text)
        text = re.sub(r'"origin" "([-\d.]+) ([-\d.]+) ([-\d.]+)"',
                      lambda m: '"origin" "%g %g %g"' % rotate(*map(float, m.groups())), text)
        source.write_text(text, encoding="utf-8")
        base = ["-game", "quake3", "-fs_basepath", directory, "-fs_homepath", root/"home", "-threads", 1]
        reference = compile_map(source, "precision-"+label+"-source")
        output = root/("precision-"+label+".map")
        run(exe, [*base, "-decompile", "-fast", "-brush-order", "rebuild", "-o", output, source.with_suffix(".bsp")], root, "precision-"+label)
        rebuilt = compile_map(output, "precision-"+label)
        assert geometry_signature(rebuilt, ordered=True) == geometry_signature(reference, ordered=True), label
        sample_points = [rotate(*p) for p in points]
        assert spatial_signature(rebuilt, sample_points) == spatial_signature(reference, sample_points), label
        precision.append({"orientation": label, "source_bsp_sha256": hashlib.sha256(reference.data).hexdigest(),
                          "brush_planes_materials_contents_order_match": True, "sampled_partitions_pvs_match": True})

    folder, source, original = saved
    base = ["-game", "quake3", "-fs_basepath", folder, "-fs_homepath", root/"home", "-threads", 1]
    # An explicit off switch and omission leave the existing recovery bytes alone.
    default, explicit = root/"default.map", root/"explicit-none.map"
    for output, options in ((default, []), (explicit, ["-group-policy", "none"])):
        run(exe, [*base, "-decompile", "-o", output, *options, source], root, output.stem)
        assert "group_inference" not in json.loads(Path(str(output)+".recovery.json").read_text())
    assert default.read_bytes() == explicit.read_bytes()
    _, report = recover(source, "budget-baseline")
    work = report["group_inference"]["work_used"]
    _, exact = recover(source, "budget-exact", ["-group-max-work", work, "-brush-order", "rebuild"])
    assert exact["group_inference"]["groups"] == report["group_inference"]["groups"]
    legacy = root/"legacy-interface.map"
    run(exe, [*base, "-convert", "-format", "map_220", "-group-policy", "surfaces", "-o", legacy, source], root, "legacy-interface")
    assert legacy.read_bytes() == (root/"budget-baseline.map").read_bytes()
    assert Path(str(legacy)+".recovery.json").is_file()

    def fails(label, options, path=source, mode="-decompile", expected=None):
        output, report = root/"preserved.map", root/"preserved.json"
        output.write_bytes(b"prior MAP"); report.write_bytes(b"prior report")
        before = path.read_bytes()
        p = subprocess.run([str(exe), *map(str, base), mode, "-o", str(output), "-report", str(report),
                            *map(str, options), str(path)], cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
        log = p.stdout.decode(errors="replace")
        (root/(label+".log")).write_text(log, encoding="utf-8")
        assert p.returncode == 1, (label, log[-4000:])
        if expected:
            assert expected in log, (label, log[-4000:])
        assert path.read_bytes() == before and output.read_bytes() == b"prior MAP" and report.read_bytes() == b"prior report"
        assert not list(root.glob("*.q3mapx-*"))

    fails("budget-minus-one", ["-group-policy", "surfaces", "-group-max-work", work-1], expected="work budget exceeded")
    bad_options = [["-group-policy", "guess"], ["-group-policy"], ["-group-max-work", 1],
                   ["-group-policy", "surfaces", "-group-max-work", 0], ["-group-policy", "surfaces", "-group-max-work", 100000001],
                   ["-group-policy", "surfaces", "-group-max-work", "1.5"], ["-group-policy", "surfaces", "-brush-order", "bsp"],
                   ["-group-policy", "surfaces", "-wtf"], ["-group-policy", "surfaces", "-readmap"],
                   ["-group-policy", "surfaces", "-format", "obj"], ["-group-policy", "surfaces", "-game", "alice"]]
    for i, options in enumerate(bad_options):
        fails(f"invalid-option-{i}", options)
    fails("map-input", ["-format", "map_220", "-group-policy", "surfaces"], source.with_suffix(".map"), "-convert")

    payloads = [original.lump(i) for i in range(17)]

    def changed(label, changes):
        lumps = payloads.copy()
        for index, data in changes.items():
            lumps[index] = data
        path = root/(label+".bsp")
        path.write_bytes(pack_file(46, lumps))
        return path

    for label, key, value, reason in (("world-index-map", "_indexmap", "unused.tga", "world_index_map"),
                                      ("world-smoothing", "_shadeangle", "60", "world_smoothing_context")):
        entities = payloads[0].replace(b'{', ('{\n"'+key+'" "'+value+'"\n').encode(), 1)
        path = changed(label, {0: entities})
        _, result = recover(path, label)
        assert result["group_inference"]["exported_groups"] == 0
        assert result["group_inference"]["export_block"] == reason
    # Multiple coincident render surfaces cannot identify unique group ownership.
    surface = report["group_inference"]["groups"][0]["surface_indices"][0]*104
    first, count = struct.unpack_from("<2i", payloads[7], 24)
    at = first+count
    duplicate = payloads[13][:at*104]+payloads[13][surface:surface+104]+payloads[13][at*104:]
    model = bytearray(payloads[7]); struct.pack_into("<i", model, 28, count+1)
    for offset in range(40, len(model), 40):
        begin = struct.unpack_from("<i", model, offset+24)[0]
        if begin >= at:
            struct.pack_into("<i", model, offset+24, begin+1)
    leaf_surfaces = b"".join(struct.pack("<i", i+(i >= at)) for (i,) in struct.iter_unpack("<i", payloads[5]))
    path = changed("overlapping-surface-support", {5: leaf_surfaces, 7: model, 13: duplicate})
    _, result = recover(path, "overlapping-surface-support")
    assert any(b["exclusion"] == "ambiguous_overlapping_surface_support" for b in result["group_inference"]["brushes"])
    assert all(not g["exported"] for g in result["group_inference"]["groups"]
               if any(b["brush_index"] in g["brush_indices"] and b["exclusion"] for b in result["group_inference"]["brushes"]))

    models = bytearray(payloads[7]); struct.pack_into("<2i", models, 64, first, count)
    _, result = recover(changed("shared-model-surfaces", {7: models}), "shared-model-surfaces")
    assert result["group_inference"]["export_block"] == "ambiguous_surface_model_ownership"
    assert result["group_inference"]["exported_groups"] == 0
    models = bytearray(payloads[7]); struct.pack_into("<i", models, 36, 50_001)
    path = changed("brush-count-limit", {7: models, 8: payloads[8]+payloads[8][:12]*50_001})
    fails("brush-count-limit", ["-group-policy", "surfaces"], path, expected="50000 world brushes")
    brushes = bytearray(payloads[8]); struct.pack_into("<2i", brushes, 0, len(payloads[9])//8, 257)
    path = changed("brush-side-limit", {8: brushes, 9: payloads[9]+payloads[9][:8]*257})
    fails("brush-side-limit", ["-group-policy", "surfaces"], path, expected="256 sides")

    result = {"rebuilds": checks, "baked_lighting_parity": lights, "sampled_points": len(points),
              "additional_fast_plane_precision_controls": precision,
              "ordered_pairs_per_rebuild": len(points)**2, "invalid_cli_controls": len(bad_options)+1,
              "scope": "Synthetic assembly controls, with a deliberately indistinguishable flat-source case and an order-blocked disconnected case; no universal authoring guarantee"}
    (root/"validation.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print(f"Surface-supported groups: {len(checks)} rebuilds, mixed detail/opacity, 3 bake controls, ambiguity, bounds and compatibility passed")


if __name__ == "__main__":
    main()

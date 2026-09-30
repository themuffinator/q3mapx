"""Independent brush-interior witnesses and known-source recovery controls.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

from integration import Bsp, run
from native_early import native_lumps as early_lumps, pack_file
from native_fakk import native_file as fakk_file
from native_mohaa import native_lumps as mohaa_lumps, pack as mohaa_pack
from recovery_classification import (brush_labels, create_case, exported_flags,
                                     geometry_signature, spatial_signature)


def verify_witnesses(bsp, report):
    """Evaluate reported points against the original planes/tree, not leaf refs."""
    planes = list(struct.iter_unpack("<4f", bsp.lump(2)))
    nodes = list(struct.iter_unpack("<9i", bsp.lump(3)))
    leaves = list(struct.iter_unpack("<12i", bsp.lump(4)))
    sides = list(struct.iter_unpack("<2i", bsp.lump(9)))
    brushes = list(struct.iter_unpack("<3i", bsp.lump(8)))
    count = 0
    for brush in report["brushes"]:
        cells = brush["interior_cells"]
        if cells["status"] != "analyzed":
            continue
        assert math.isclose(cells["brush_volume"], cells["fragment_volume"], rel_tol=1e-6, abs_tol=1e-6)
        if report["visibility"]["present"]:
            vis = bsp.lump(16)
            _, stride = struct.unpack_from("<2i", vis)
            clusters = cells["interior_clusters"]
            assert cells["tested_pvs_pairs"] == len(clusters)**2
            assert cells["invisible_pvs_pairs"] == sum(not (vis[8+a*stride+b//8] & (1 << (b % 8)))
                                                        for a in clusters for b in clusters)
        first, size, _ = brushes[brush["index"]]
        for kind in ("open_witness", "opaque_witness"):
            witness = cells[kind]
            if witness is None:
                continue
            point = witness["point"]

            def distance(plane):
                return (sum(a*b for a, b in zip(plane[:3], point))-plane[3]) / math.hypot(*plane[:3])

            margins = [-distance(planes[p]) for p, _ in sides[first:first+size]]
            node = report["world_graph"]["head"]
            for _ in range(len(nodes)+1):
                if node < 0:
                    break
                plane, front, back, *_ = nodes[node]
                d = distance(planes[plane])
                margins.append(abs(d))
                node = front if d >= 0 else back
            else:
                raise AssertionError("Unbounded witness traversal")
            assert -1-node == witness["leaf"]
            assert leaves[-1-node][0] == witness["cluster"]
            assert (witness["cluster"] >= 0) == (kind == "open_witness")
            assert min(margins) >= .01-1e-9
            assert math.isclose(min(margins), witness["clearance"], rel_tol=1e-8, abs_tol=1e-8)
            count += 1
    return count


def without_references(bsp, removed):
    lumps = [bsp.lump(i) for i in range(17)]
    references = [i[0] for i in struct.iter_unpack("<i", lumps[6])]
    leaves, kept = [], []
    for leaf in struct.iter_unpack("<12i", lumps[4]):
        leaf = list(leaf)
        selected = [i for i in references[leaf[10]:leaf[10]+leaf[11]] if i not in removed]
        leaf[10], leaf[11] = len(kept), len(selected)
        kept += selected
        leaves.append(struct.pack("<12i", *leaf))
    lumps[4] = b"".join(leaves)
    lumps[6] = b"".join(struct.pack("<i", i) for i in kept)
    return lumps


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(strict=True), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    base = ["-game", "quake3", "-fs_basepath", root, "-fs_homepath", root/"home", "-threads", 1]
    checks, witnesses = [], 0

    def evidence(path, label, *, options=(), threads=1, game="quake3"):
        before = path.read_bytes()
        output = root/(label+".json")
        run(exe, [*base, "-game", game, "-threads", threads, "-bsp-evidence", "-brush-cells",
                  "-region-depth", 0, "-report", output, *options, path], root, label)
        assert path.read_bytes() == before
        report = json.loads(output.read_text())
        assert report["source"]["sha256"] == hashlib.sha256(before).hexdigest()
        assert not report["brush_cell_analysis"]["uses_stored_leaf_brush_references"]
        assert not report["brush_cell_analysis"]["author_classification_proven"]
        return report

    def recover(path, label, *, options=(), policy="cells", threads=1):
        before = path.read_bytes()
        output = root/(label+".map")
        run(exe, [*base, "-threads", threads, "-decompile", "-detail-policy", policy,
                  "-brush-order", "rebuild", "-o", output, *options, path], root, label)
        assert path.read_bytes() == before
        return output, json.loads(Path(str(output)+".recovery.json").read_text())

    def fails(path, label, options, *, mode="-decompile", expected=None):
        map_output = mode in ("-decompile", "-convert")
        output = root/("preserved.map" if map_output else "preserved.json")
        output.write_bytes(b"previous output\n")
        report = root/"preserved.recovery.json"
        report.write_bytes(b"previous report\n")
        destination = ["-o", output, "-report", report] if map_output else ["-report", output]
        before = path.read_bytes()
        result = subprocess.run([str(exe), *map(str, base), mode, *map(str, options),
                                 *map(str, destination), str(path)], cwd=root,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
        log = result.stdout.decode(errors="replace")
        (root/(label+".log")).write_text(log, encoding="utf-8")
        assert result.returncode == 1, (label, result.returncode, log[-4000:])
        if expected:
            assert expected in log, (label, log[-4000:])
        assert path.read_bytes() == before
        assert output.read_bytes() == b"previous output\n"
        assert report.read_bytes() == b"previous report\n"
        assert not list(root.glob("*.q3mapx-*"))

    points = [(x, y, z) for x in (-281, -261, -199, -127, -63, 1, 65, 129, 201, 263, 283)
              for y in (-281, -199, -127, -63, 1, 65, 129, 201, 283) for z in (-7, 31, 117, 223, 263, 283)]
    for mode in ("detail", "structural", "mixed-group", "translucent-structural", "mixed-opacity", "side-structural"):
        folder = root/mode
        source, labels = create_case(folder, "mixed-opacity" if mode == "side-structural" else mode)
        if mode == "side-structural":
            # A non-first side alone protects these structural brushes. The old
            # brush-shader shortcut cannot see that side's structural semantics.
            source.write_text(source.read_text().replace("q3mapx/opaque_structural", "q3mapx/stone"), encoding="utf-8")
        base = ["-game", "quake3", "-fs_basepath", folder, "-fs_homepath", root/"home", "-threads", 1]

        def compile_map(path, label):
            run(exe, [*base, "-meta", path], root, label+"-bsp")
            assert not path.with_suffix(".lin").exists(), label+" leaked"
            run(exe, [*base, "-vis", "-saveprt", "-reproducible", path], root, label+"-vis")
            return Bsp(path.with_suffix(".bsp"))

        original = compile_map(source, mode)
        path = source.with_suffix(".bsp")
        expected = brush_labels(original, labels)
        observed = evidence(path, mode+"-cells")
        assert observed == evidence(path, mode+"-cells-4", threads=4)
        witnesses += verify_witnesses(original, observed)
        if mode in ("detail", "structural", "mixed-group"):
            for i, detail in expected.items():
                cells = observed["brushes"][i]["interior_cells"]
                assert cells["status"] == "analyzed" and cells["uncertain_fragments"] == 0
                assert bool(cells["open_witness"]) == detail
                assert bool(cells["opaque_witness"]) != detail
        damaged = folder/"missing-brush-references.bsp"
        removed = {i for i, detail in expected.items() if detail}
        damaged.write_bytes(pack_file(46, without_references(original, removed)))
        report = evidence(damaged, mode+"-missing-references")
        assert [b["interior_cells"] for b in report["brushes"]] == [b["interior_cells"] for b in observed["brushes"]]
        assert all(report["brushes"][i]["leaf_references"] == 0 for i in removed)
        input_path = damaged if removed else path
        legacy, legacy_report = recover(input_path, mode+"-legacy", policy="legacy")
        assert "detail_inference" not in legacy_report
        legacy_flags = exported_flags(legacy)
        legacy_errors = sum(legacy_flags[i] != {detail} for i, detail in expected.items())
        assert legacy_errors == (len(removed) if removed else 5 if mode == "side-structural" else 0)
        # No option must keep the previous MAP bytes and report policy.
        default = root/(mode+"-default.map")
        run(exe, [*base, "-decompile", "-brush-order", "rebuild", "-o", default, input_path], root, mode+"-default")
        assert default.read_bytes() == legacy.read_bytes()
        geometry = geometry_signature(original, ordered=True)
        spatial = spatial_signature(original, points)
        for fast in (False, True):
            for fmt in ("map", "map_bp", "map_220"):
                label = f'{mode}-{"fast" if fast else "full"}-{fmt}'
                options = ["-format", fmt, *(["-fast"] if fast else [])]
                output, recovered = recover(input_path, label, options=options)
                flags = exported_flags(output)
                assert all(flags[i] == {detail} for i, detail in expected.items()), (label, flags, expected)
                inference = recovered["detail_inference"]
                assert not inference["author_classification_proven"] and not inference["bsp_rebuild_validated"]
                assert not inference["original_shader_assets_verified"]
                assert all(d["applied_detail"] == detail for i, detail in expected.items()
                           for d in [inference["brushes"][i]])
                assert all(d["baseline_detail"] == d["applied_detail"] and d["current_material_compile_flags"] is None
                           for d in inference["brushes"] if d["reason"] == "non_world_geometry_preserved")
                rebuilt = compile_map(output, label)
                assert geometry_signature(rebuilt, ordered=True) == geometry, label
                assert spatial_signature(rebuilt, points) == spatial, label
                assert rebuilt.summary()["nodes"] == original.summary()["nodes"], label
                assert rebuilt.lump(0).split(b'}', 1)[1] == original.lump(0).split(b'}', 1)[1], label
                if fast and fmt == "map_220":
                    parallel, other = recover(input_path, label+"-4", options=options, threads=4)
                    assert parallel.read_bytes() == output.read_bytes()
                    assert other["detail_inference"] == inference
        checks.append({"case": mode, "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                       "bsp_sha256": hashlib.sha256(original.data).hexdigest(), "world_brushes": len(expected),
                       "removed_detail_references": len(removed), "legacy_classification_errors": legacy_errors,
                       "cells_classification_errors": 0, "formats": 3, "fast_and_full": True,
                       "rebuild_geometry_order_material_contents_match": True, "sampled_spatial_pvs_match": True})
        if mode == "detail":
            saved = original, path, input_path, folder, removed

    original, source, damaged, folder, removed = saved
    base = ["-game", "quake3", "-fs_basepath", folder, "-fs_homepath", root/"home", "-threads", 1]
    payloads = [original.lump(i) for i in range(17)]

    # Current assets can differ from the original bake. Protection must prevent
    # promoting open witnesses when those current material semantics disagree.
    shader = folder/"baseq3/scripts/q3mapx_tests.shader"
    original_shader = shader.read_bytes()
    try:
        for name, parameters, reason in (
                ("hint", "surfaceparm hint", "protected_material_preserved"),
                ("sky", "surfaceparm sky", "protected_material_preserved"),
                ("nonopaque", "surfaceparm nonsolid\n surfaceparm trans", "nonopaque_material_preserved")):
            shader.write_text(original_shader.decode().replace("qer_editorimage", parameters+"\n qer_editorimage"), encoding="utf-8")
            _, recovered = recover(damaged, "current-material-"+name)
            assert all(not recovered["detail_inference"]["brushes"][i]["applied_detail"] for i in removed)
            assert all(recovered["detail_inference"]["brushes"][i]["reason"] == reason for i in removed)
    finally:
        shader.write_bytes(original_shader)

    def changed(label, lumps):
        path = root/(label+".bsp")
        path.write_bytes(pack_file(46, lumps))
        return path

    # Missing VIS is unknown, not all-visible. Geometric witnesses survive it.
    no_vis = without_references(original, removed)
    no_vis[16] = b""
    path = changed("without-vis", no_vis)
    report = evidence(path, "without-vis")
    assert not report["visibility"]["present"]
    assert all(b["interior_cells"]["tested_pvs_pairs"] is None for b in report["brushes"])
    _, recovered = recover(path, "without-vis-recovery")
    assert all(recovered["detail_inference"]["brushes"][i]["applied_detail"] for i in removed)
    # Self-inconsistent PVS suppresses geometric promotion; an all-visible PVS
    # is merely a coarse observation and never described as authoring proof.
    count, _ = struct.unpack_from("<2i", payloads[16])
    stride = (count+7)//8+3
    pvs = struct.pack("<2i", count, stride)+bytes([255])*(count*stride)
    all_vis = without_references(original, removed)
    all_vis[16] = pvs
    path = changed("all-visible", all_vis)
    report = evidence(path, "all-visible")
    assert report["visibility"]["all_visible"]
    assert all(b["interior_cells"]["invisible_pvs_pairs"] == 0 for b in report["brushes"])
    bad_pvs = bytearray(pvs)
    bad_pvs[8] &= ~1
    all_vis[16] = bad_pvs
    path = changed("inconsistent-pvs", all_vis)
    _, recovered = recover(path, "inconsistent-pvs-recovery")
    assert recovered["detail_inference"]["pvs_missing_self_bits"] == 1
    assert all(not recovered["detail_inference"]["brushes"][i]["applied_detail"] for i in removed)
    assert all(recovered["detail_inference"]["brushes"][i]["reason"] == "inconsistent_pvs_preserved" for i in removed)

    # Analytical convex solids supplement known-source MAPs. Their leaf-reference
    # lists intentionally remain unrelated to the replacement geometry.
    target = min(removed)

    def solid(label, planes):
        lumps = payloads.copy()
        first_plane, first_side = len(lumps[2])//16, len(lumps[9])//8
        lumps[2] += b"".join(struct.pack("<8f", *p, *[-v for v in p]) for p in planes)
        lumps[9] += b"".join(struct.pack("<2i", first_plane+i*2, 0) for i in range(len(planes)))
        brushes = bytearray(lumps[8])
        struct.pack_into("<3i", brushes, target*12, first_side, len(planes), 0)
        lumps[8] = brushes
        path = changed(label, lumps)
        report = evidence(path, label)
        assert report == evidence(path, label+"-4", threads=4)
        return path, report, report["brushes"][target]["interior_cells"]

    cube = [(-1, 0, 0, 12), (1, 0, 0, 12), (0, -1, 0, 12), (0, 1, 0, 12), (0, 0, -1, -32), (0, 0, 1, 56)]
    analytical = []
    for label, planes, expected_volume in (
            ("oblique-half-cube", [*cube, (2, 2, 0, 0)], 24**3/2),
            ("oblique-tetrahedron", [*cube, (1, 1, 1, 32)], 24**3/6),
            ("overlapping-floor", [*cube[:4], (0, 0, -1, 8), (0, 0, 1, 8)], 24*24*16),
            ("thin-sliver", [(-1, 0, 0, .001), (1, 0, 0, .001), *cube[2:]], .002*24*24),
            ("translated-cube", [(-1, 0, 0, -8988), (1, 0, 0, 9012), *cube[2:]], 24**3),
            ("redundant-planes", [*cube, *cube], 24**3)):
        path, report, cell = solid(label, planes)
        assert cell["status"] == "analyzed", (label, cell)
        assert math.isclose(cell["brush_volume"], expected_volume, rel_tol=1e-6)
        witnesses += verify_witnesses(Bsp(path), report)
        if label == "overlapping-floor":
            assert cell["open_witness"] and cell["opaque_witness"]
        elif label == "thin-sliver":
            assert not cell["open_witness"] and not cell["opaque_witness"] and cell["uncertain_fragments"]
        elif label == "translated-cube":
            assert not cell["open_witness"] and cell["opaque_witness"]
        else:
            assert cell["open_witness"] and not cell["opaque_witness"]
        analytical.append({"case": label, "expected_volume": expected_volume, "reported_volume": cell["brush_volume"]})
    for label, planes, status in (
            ("empty-solid", [*cube, (1, 1, 0, -48)], "empty_or_degenerate"),
            ("degenerate-solid", [*cube, (1, 1, 0, -24)], "empty_or_degenerate"),
            ("no-axial-enclosure", cube[1:], "axial_enclosure_unavailable"),
            ("too-many-sides", cube+[cube[0]]*251, "geometry_limit"),
            ("coordinate-limit", [(-1, 0, 0, -10000016), (1, 0, 0, 10000032), *cube[2:]], "coordinate_limit")):
        _, _, cell = solid(label, planes)
        assert cell["status"] == status, (label, cell)
        assert not cell["open_witness"] and not cell["opaque_witness"]

    invalid = payloads.copy()
    invalid[2] += bytes(16)
    fails(changed("invalid-plane", invalid), "invalid-plane", ["-brush-cells"],
          mode="-bsp-evidence", expected="degenerate")

    single = payloads.copy()
    single[3] = b""
    single[4] = struct.pack("<12i", 0, 0, -32768, -32768, -32768, 32768, 32768, 32768, 0, 0, 0, 0)
    single[16] = struct.pack("<2iB", 1, 1, 1)
    path = changed("single-leaf", single)
    report = evidence(path, "single-leaf")
    assert report["world_graph"]["head"] == -1
    assert all(b["interior_cells"]["open_witness"]["leaf"] == 0 for b in report["brushes"] if b["model"] == 0)
    witnesses += verify_witnesses(Bsp(path), report)

    shared = payloads.copy()
    nodes = bytearray(shared[3])
    child = next(i for i in struct.unpack_from("<2i", nodes, 4) if i >= 0)
    struct.pack_into("<2i", nodes, 4, child, child)
    shared[3] = nodes
    path = changed("shared-tree", shared)
    report = evidence(path, "shared-tree")
    assert all(b["interior_cells"]["status"] == "world_tree_unavailable" for b in report["brushes"] if b["model"] == 0)
    _, recovered = recover(path, "shared-tree-recovery")
    assert all(d["baseline_detail"] == d["applied_detail"] for d in recovered["detail_inference"]["brushes"])

    report = evidence(source, "budget-baseline")
    limit = report["limits"]["work_units_used"]
    exact = evidence(source, "budget-exact", options=["-max-work", limit], threads=4)
    assert exact["brushes"] == report["brushes"]
    fails(source, "budget-minus-one", ["-brush-cells", "-region-depth", 0, "-max-work", limit-1],
          mode="-bsp-evidence", expected="work budget exceeded")
    _, recovered = recover(damaged, "recovery-budget-baseline")
    converted = root/"legacy-interface.map"
    run(exe, [*base, "-convert", "-format", "map_220", "-brush-order", "rebuild", "-detail-policy", "cells",
              "-o", converted, damaged], root, "legacy-interface")
    assert json.loads(Path(str(converted)+".recovery.json").read_text())["detail_inference"] == recovered["detail_inference"]
    assert converted.read_bytes() == (root/"recovery-budget-baseline.map").read_bytes()
    limit = recovered["detail_inference"]["work_used"]
    _, exact = recover(damaged, "recovery-budget-exact", options=["-detail-max-work", limit], threads=4)
    assert exact["detail_inference"]["brushes"] == recovered["detail_inference"]["brushes"]
    fails(damaged, "recovery-budget-minus-one", ["-detail-policy", "cells", "-detail-max-work", limit-1], expected="work budget exceeded")
    invalid_options = [
        ["-detail-policy", "guess"], ["-detail-policy"], ["-detail-max-work", 1],
        ["-detail-policy", "cells", "-detail-max-work", 0],
        ["-detail-policy", "cells", "-detail-max-work", 100000001],
        ["-detail-policy", "cells", "-detail-max-work", "1.5"],
        ["-detail-policy", "cells", "-format", "obj"],
        ["-detail-policy", "cells", "-readmap"], ["-detail-policy", "cells", "-wtf"],
        ["-game", "alice", "-detail-policy", "cells"],
    ]
    for i, options in enumerate(invalid_options):
        fails(source, f"invalid-option-{i}", options)
    # -decompile always resolves its input to .bsp, including a .map basename.
    # -convert with a MAP input really would load source, so reject that mode.
    fails(source.with_suffix(".map"), "map-input", ["-format", "map_220", "-detail-policy", "cells"], mode="-convert")

    # Unowned records are still represented in inference provenance. They can
    # expand a small input beyond the report ceiling without increasing exported
    # geometry. Exercise the actual limit and both prior output files.
    oversized = payloads.copy()
    oversized[8] += payloads[8][:12]*200_000
    fails(changed("recovery-report-ceiling", oversized), "recovery-report-ceiling",
          ["-detail-policy", "cells", "-fast"], expected="report exceeds 64 MiB")

    native = []
    profiles = [("alice", fakk_file(original, 42)), ("fakk2", fakk_file(original, 12)),
                ("mohaa", mohaa_pack(mohaa_lumps(original)))]
    profiles += [(game, pack_file(v, early_lumps(original, v))) for v, game in ((43, "q3-ihv"), (44, "q3test44"), (45, "q3test45"))]
    for game, data in profiles:
        path = root/(game+".bsp")
        path.write_bytes(data)
        report = evidence(path, "native-"+game, game=game)
        cells = [b["interior_cells"] for b in report["brushes"] if b["model"] == 0]
        assert len(cells) == 15 and all(c["status"] == "analyzed" for c in cells)
        assert sum(bool(c["open_witness"]) for c in cells) == 9
        native.append(game)

    early = early_lumps(original, 44)
    nodes = bytearray(early[2])
    for offset in range(0, len(nodes), 36):
        for at in (offset+4, offset+8):
            child = struct.unpack_from("<i", nodes, at)[0]
            if child >= 0:
                struct.pack_into("<i", nodes, at, child+1)
    early[2] = struct.pack("<9i", 0, -1, -1, -128, -128, -128, 128, 128, 128)+nodes
    models = bytearray(early[6])
    for offset in range(0, len(models), 48):
        head = struct.unpack_from("<i", models, offset+36)[0]
        if head >= 0:
            struct.pack_into("<i", models, offset+36, head+1)
    early[6] = models
    path = root/"native-relocated-root.bsp"
    path.write_bytes(pack_file(44, early))
    report = evidence(path, "native-relocated-root", game="q3test44")
    assert report["world_graph"]["head"] == 1
    assert sum(bool(b["interior_cells"]["open_witness"]) for b in report["brushes"] if b["model"] == 0) == 9

    result = {"known_source": checks, "rebuilds": 36, "sampled_points_per_rebuild": len(points),
              "ordered_pairs_per_rebuild": len(points)**2, "independently_verified_witnesses": witnesses,
              "analytical_controls": analytical, "native_evidence_profiles": native,
              "invalid_cli_controls": len(invalid_options)+1,
              "scope": "Synthetic known-source controls; no universal author-classification or rebuild guarantee"}
    (root/"validation.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    print("Brush-cell witnesses, damaged references, material protection, 36 rebuilds, bounds, budgets and worker parity passed")


if __name__ == "__main__":
    main()

"""Known-source and adversarial BSP evidence checks; no installed assets.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

from fixtures import box, create_fixture
from integration import Bsp, run
from native_early import native_lumps as early_lumps, pack_file
from native_fakk import native_file as fakk_file
from native_mohaa import native_lumps as mohaa_lumps, pack as mohaa_pack


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    # Keep compiler home lookup inside the generated test project.
    common = ["-fs_basepath", root, "-fs_homepath", root / "home"]

    def evidence(path, label, *, game="quake3", options=(), threads=1, output=None):
        data = path.read_bytes()
        output = output or root / f"{label}.json"
        run(exe, ["-game", game, *common, "-threads", threads, "-bsp-evidence",
                  "-report", output, *options, path], root, label)
        result = json.loads(output.read_text(encoding="utf-8"))
        assert path.read_bytes() == data, label
        assert result["schema_version"] == 1 and result["report_kind"] == "bsp_evidence"
        assert result["source"]["sha256"] == hashlib.sha256(data).hexdigest()
        assert result["source"]["bytes"] == len(data)
        assert result["source"]["geometry_validated"]
        return result

    def fails(path, label, *, options=(), expected=None, output=None, game="quake3", preexec=None):
        output = output or root / "preserved.json"
        before = path.read_bytes()
        saved = output.read_bytes() if output.is_file() else None
        command = [str(exe), "-game", game, *map(str, common), "-bsp-evidence",
                   "-report", str(output), *map(str, options), str(path)]
        result = subprocess.run(command, cwd=root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=30, preexec_fn=preexec)
        log = result.stdout.decode(errors="replace")
        (root / f"{label}.log").write_text(log, encoding="utf-8")
        assert result.returncode == 1, (label, result.returncode, log)
        if expected:
            assert expected in log, (label, log)
        assert path.read_bytes() == before
        if saved is not None:
            assert output.read_bytes() == saved, label
        assert not list(root.glob("*.q3mapx-*")), "Abandoned report staging files"

    reports, bsps = {}, {}
    labels = []
    for mode in ("structural", "detail"):
        folder = root / mode
        source = create_fixture(folder, dense=True, grid=5, patch=False)
        if mode == "detail":
            text = source.read_text(encoding="utf-8")
            for y in range(-256, 257, 128):
                for x in range(-256, 257, 128):
                    brush = box((x-20, y-20, 0), (x+20, y+20, 80+(x+y)%112))
                    assert text.count(brush) == 1
                    text = text.replace(brush, brush.replace(" 0 0 0\n", " 134217728 0 0\n"))
            source.write_text(text, encoding="utf-8")
        source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
        base = ["-game", "quake3", "-fs_basepath", folder, "-fs_homepath", root / "home", "-threads", 2]
        run(exe, [*base, "-meta", source], root, f"{mode}-bsp")
        path = source.with_suffix(".bsp")
        before_vis = evidence(path, f"{mode}-without-vis")
        assert not before_vis["visibility"]["present"]
        assert before_vis["visibility"]["visible_pairs"] is None
        run(exe, [*base, "-vis", "-saveprt", source], root, f"{mode}-vis")
        report = evidence(path, mode, options=["-region-depth", 0])
        assert report == evidence(path, f"{mode}-threads4", options=["-region-depth", 0], threads=4)
        assert hashlib.sha256(source.read_bytes()).hexdigest() == source_hash
        assert report["visibility"]["present"]
        assert report["visibility"]["missing_self_bits"] == 0
        assert report["world_graph"]["unique_node_paths"]
        assert len(report["regions"]) == 1
        assert report["regions"][0]["split_nodes"] == report["world_graph"]["reachable_nodes"]
        pillars = []
        for brush in report["brushes"]:
            assert brush["model_ownership"] == "unique"
            assert set(brush["leaf_path_partition_side_indices"]) <= set(brush["world_partition_side_indices"])
            bounds = brush["axial_plane_enclosure"]
            if brush["model"] == 0 and bounds and bounds["maxs"][0]-bounds["mins"][0] == 40:
                assert bounds["maxs"][1]-bounds["mins"][1] == 40
                pillars.append(brush)
        assert len(pillars) == 25
        assert any(b["model"] == 1 for b in report["brushes"]), "Door model ownership lost"
        reports[mode], bsps[mode] = report, Bsp(path)
        labels.append({"source_label": mode, "source_sha256": source_hash,
                       "bsp_sha256": report["source"]["sha256"], "pillars": 25,
                       "nodes": report["world_graph"]["reachable_nodes"],
                       "clusters": report["visibility"]["stored_clusters"],
                       "local_plane_sides": sum(len(p["leaf_path_partition_side_indices"]) for p in pillars),
                       "global_plane_sides": sum(len(p["world_partition_side_indices"]) for p in pillars)})
    assert labels[0]["nodes"] > labels[1]["nodes"]
    assert labels[0]["local_plane_sides"] > labels[1]["local_plane_sides"]
    # Deliberately retain the detail pillars' coplanar floor matches as evidence
    # against classifying brushes solely by a partition-plane association.
    assert labels[1]["local_plane_sides"] > 0

    bsp = bsps["structural"]
    source = root / "structural/baseq3/maps/fixture.bsp"
    baseline = reports["structural"]
    run(exe,["-game","quake3",*common,"-bsp-evidence",source],root,"default-report")
    assert source.with_suffix(".evidence.json").is_file()
    sentinel = root / "preserved.json"
    sentinel.write_bytes(b"previous evidence must survive\n")
    fails(source, "work-budget", options=["-max-work", 1], expected="work budget exceeded")
    fails(source, "last-work-unit", options=["-max-work", baseline["limits"]["work_units_used"]-1], expected="work budget exceeded")
    exact = evidence(source, "exact-work-budget", options=["-region-depth", 0, "-max-work", baseline["limits"]["work_units_used"]])
    assert exact["brushes"] == baseline["brushes"]
    fails(source, "invalid-depth", options=["-region-depth", 9], expected="expects an integer")
    fails(source, "unknown-option", options=["-guess"], expected="Usage:")
    directory_output = root / "directory.json"
    directory_output.mkdir(exist_ok=True)
    fails(source, "directory-output", output=directory_output)
    disguised = root / "input.json"
    disguised.write_bytes(bsp.data)
    fails(disguised, "protect-same-file", output=disguised, expected="must not replace its input")
    alias = root / "input-alias.json"
    if not alias.exists():
        os.link(disguised, alias)  # generated fixture only, never an installation
    fails(disguised, "protect-hardlink", output=alias, expected="must not replace its input")
    fails(source, "wrong-profile-force", game="ja", options=["-force"], expected="does not match")

    payloads = [bsp.lump(i) for i in range(17)]

    def changed(name, lump, data):
        altered = payloads.copy()
        altered[lump] = data
        path = root / f"{name}.bsp"
        path.write_bytes(pack_file(46, altered))
        return path

    # Full PVS with deliberately nonzero tail bits and seven bytes of row padding.
    n = baseline["visibility"]["stored_clusters"]
    stride = (n+7)//8 + 7
    padded = struct.pack("<2i", n, stride) + bytes([255])*(n*stride)
    report = evidence(changed("padded-pvs", 16, padded), "padded-pvs")
    assert report["visibility"]["visible_pairs"] == n*n
    assert report["visibility"]["all_visible"] and report["visibility"]["density"] == 1
    missing_self = bytearray(padded)
    missing_self[8] &= ~1
    report = evidence(changed("missing-self", 16, missing_self), "missing-self")
    assert report["visibility"]["visible_pairs"] == n*n-1
    assert report["visibility"]["missing_self_bits"] == 1

    # A physically separate, reversed plane record still denotes the same cut.
    plane_index = struct.unpack_from("<i", payloads[3])[0]
    plane = struct.unpack_from("<4f", payloads[2], plane_index*16)
    altered = payloads.copy()
    altered[2] += struct.pack("<8f", *[-v for v in plane], *plane)
    nodes = bytearray(payloads[3])
    struct.pack_into("<i", nodes, 0, len(payloads[2])//16)
    altered[3] = nodes
    reversed_path = root / "reversed-plane.bsp"
    reversed_path.write_bytes(pack_file(46, altered))
    report = evidence(reversed_path, "reversed-plane", options=["-region-depth", 0])
    assert report["brushes"] == baseline["brushes"]

    # A cut in the negative-X region has the same plane as a brush in positive
    # X. It is a global association only, never on a leaf path for that brush.
    # This isolates spatial context from the paired maps' shared floor ambiguity.
    isolated = [b"" for _ in range(17)]
    isolated[0] = b'{\n"classname" "worldspawn"\n}\n\0'
    isolated[1] = b"textures/q3mapx/stone\0".ljust(64,b"\0") + struct.pack("<2i",0,1)
    planes = [(-1,0,0,-10),(1,0,0,20),(0,-1,0,0),(0,1,0,10),(0,0,-1,0),(0,0,1,10),(1,0,0,0)]
    isolated[2] = b"".join(struct.pack("<8f",*p,*[-v for v in p]) for p in planes)
    isolated[3] = (struct.pack("<9i",12,-1,1,-128,-128,-128,128,128,128)
                   + struct.pack("<9i",4,-2,-3,-128,-128,-128,0,128,128))
    isolated[4] = b"".join(struct.pack("<12i",0,0,-128,-128,-128,128,128,128,0,0,0,int(i==0)) for i in range(3))
    isolated[6] = struct.pack("<i",0)
    isolated[7] = struct.pack("<6f4i",-128,-128,-128,128,128,128,0,0,0,1)
    isolated[8] = struct.pack("<3i",0,6,0)
    isolated[9] = b"".join(struct.pack("<2i",i*2,0) for i in range(6))
    isolated_path = root / "spatial-plane-control.bsp"
    isolated_path.write_bytes(pack_file(46,isolated))
    report = evidence(isolated_path,"spatial-plane-control")
    assert report["brushes"][0]["world_partition_side_indices"] == [2]
    assert report["brushes"][0]["leaf_path_partition_side_indices"] == []
    assert report["brushes"][0]["axial_plane_enclosure"] == {"mins":[10,0,0],"maxs":[20,10,10]}

    # Shared acyclic nodes remain inspectable, but unique path/subtree evidence
    # is explicitly unavailable instead of exponential graph expansion.
    nodes = bytearray(payloads[3])
    children = struct.unpack_from("<2i", nodes, 4)
    child = next(c for c in children if c >= 0)
    struct.pack_into("<2i", nodes, 4, child, child)
    report = evidence(changed("shared-node", 3, nodes), "shared-node")
    assert not report["world_graph"]["leaf_path_analysis_available"]
    assert report["regions"] == []
    assert all(b["leaf_path_partition_side_indices"] is None for b in report["brushes"])
    struct.pack_into("<i", nodes, 4, 0)
    fails(changed("cycle", 3, nodes), "cycle", expected="cycle in node graph")
    invalid = bytearray(payloads[9])
    struct.pack_into("<i", invalid, 0, 0x7fffffff)
    fails(changed("bad-side", 9, invalid), "bad-side", expected="Invalid BSP:")

    # Range overlap is distinct from unique or unowned model membership.
    models = bytearray(payloads[7])
    models[40+32:40+40] = models[32:40]
    report = evidence(changed("overlapping-models", 7, models), "overlapping-models")
    assert any(b["model_ownership"] == "overlapping" and b["model"] is None for b in report["brushes"])

    # A compact input can imply a much larger per-brush report. Exercise the real
    # streaming ceiling and staged-file cleanup, without first allocating JSON.
    large = payloads.copy()
    large[8] = payloads[8][:12]*200_000
    models = bytearray(payloads[7])
    struct.pack_into("<i",models,36,200_000)
    large[7] = models
    large_path = root / "report-ceiling.bsp"
    large_path.write_bytes(pack_file(46,large))
    fails(large_path,"report-ceiling",expected="report exceeds 64 MiB")

    native_results = []
    cases = [("alice", fakk_file(bsp, 42)), ("fakk2", fakk_file(bsp, 12)), ("mohaa", mohaa_pack(mohaa_lumps(bsp)))]
    cases += [(game, pack_file(v, early_lumps(bsp, v))) for v, game in ((43,"q3-ihv"),(44,"q3test44"),(45,"q3test45"))]
    for game, data in cases:
        path = root / f"{game}.bsp"
        path.write_bytes(data)
        result = evidence(path, f"native-{game}", game=game)
        assert result["counts"]["brushes"] == baseline["counts"]["brushes"]
        assert sum(b["model"] == 1 for b in result["brushes"]) == 1
        if game in ("q3-ihv", "q3test44"):
            assert any(b["index"] != b["source_brush_index"] for b in result["brushes"])
        if game == "mohaa":
            assert result["counts"]["native_terrain_triangles"] > 0
            assert result["counts"]["unloaded_static_model_instances"] == 1
        native_results.append({"game": game, "sha256": result["source"]["sha256"], "counts": result["counts"]})

    # Early formats retain explicit model tree roots: move the world root away
    # from zero and keep the analysis attached to the native head node.
    early = early_lumps(bsp,44)
    nodes = bytearray(early[2])
    for offset in range(0,len(nodes),36):
        for at in (offset+4,offset+8):
            child = struct.unpack_from("<i",nodes,at)[0]
            if child >= 0:
                struct.pack_into("<i",nodes,at,child+1)
    early[2] = struct.pack("<9i",0,-1,-1,-128,-128,-128,128,128,128) + nodes
    models = bytearray(early[6])
    for offset in range(0,len(models),48):
        head = struct.unpack_from("<i",models,offset+36)[0]
        if head >= 0:
            struct.pack_into("<i",models,offset+36,head+1)
    early[6] = models
    relocated = root / "early-relocated-root.bsp"
    relocated.write_bytes(pack_file(44,early))
    relocated_report = evidence(relocated,"early-relocated-root",game="q3test44")
    assert relocated_report["world_graph"]["head"] == 1
    assert relocated_report["world_graph"]["reachable_nodes"] == baseline["world_graph"]["reachable_nodes"]

    if os.name != "nt":
        import resource
        import signal

        def short_write():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (128, 128))

        fails(source, "short-write", preexec=short_write)

    evidence_path = root / "validation.json"
    evidence_path.write_text(json.dumps({"known_source_pairs": labels, "native_profiles": native_results,
        "scope": "Evidence extraction, not inference accuracy or optimization safety",
        "checks": ["SHA-256/input preservation", "1/4-worker exact report parity", "missing VIS",
                   "known structural/detail labels", "coplanar detail ambiguity", "submodel ownership",
                   "work budget and preserved output", "streaming 64 MiB ceiling", "default report path",
                   "source/hardlink protection", "PVS tail/padding",
                   "reversed duplicate planes", "spatially separate coplanar brush", "shared DAG/cycle", "bad plane references",
                   "overlapping model ranges", "six recovery-only profiles", "native nonzero world root",
                   "Linux buffered write failure" if os.name != "nt" else "Windows output rejection"]}, indent=2)+"\n", encoding="utf-8")
    print("BSP evidence: known-source pairs, native profiles, bounded work, PVS and output protection passed")


if __name__ == "__main__":
    main()

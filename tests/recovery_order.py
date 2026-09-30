"""Rebuild ordering and its limits against original MAPs. GPL-3.0-or-later."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from fixtures import box
from integration import Bsp, run
from native_early import pack_file
from recovery_classification import create_case, geometry_signature, spatial_signature


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(strict=True), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    points = [(x, y, z) for x in (-281, -261, -199, -127, -63, 1, 65, 129, 201, 263, 283)
              for y in (-281, -199, -127, -63, 1, 65, 129, 201, 283) for z in (-7, 31, 117, 223, 263, 283)]
    checks = []
    cases = ("detail", "structural", "mixed-group", "translucent-structural", "mixed-opacity", "structural-override")
    for mode in cases:
        directory = root / mode
        source, _ = create_case(directory, mode)
        # Multiple brushes in a surviving entity with a nonzero origin exercise
        # model-local ordering independently of the world's splitter selection.
        text = source.read_text(encoding="utf-8")
        origin = box((104, -8, 56), (120, 8, 72), "common/origin", "0 0 0 1 1")
        assert text.count(origin) == 1
        material = "q3mapx/structural" if mode in ("translucent-structural", "mixed-opacity") else "q3mapx/stone"
        text = text.replace(origin, box((96, -48, 112), (128, -32, 144))
                            + box((96, 32, 112), (128, 48, 144), material) + origin)
        source.write_text(text, encoding="utf-8")
        source_bytes = source.read_bytes()
        base = ["-game", "quake3", "-fs_basepath", directory, "-fs_homepath", root/"home", "-threads", 1]

        def compile_map(path, label):
            run(exe, [*base, "-meta", path], root, label+"-bsp")
            assert not path.with_suffix(".lin").exists(), label+" leaked"
            run(exe, [*base, "-vis", "-reproducible", "-saveprt", path], root, label+"-vis")
            return Bsp(path.with_suffix(".bsp"))

        original = compile_map(source, mode+"-source")
        original_geometry = geometry_signature(original, ordered=True)
        signature = spatial_signature(original, points)
        source_nodes = original.summary()["nodes"]
        # Explicitly contradictory numeric detail bits on structural materials
        # affect splitter priorities although the brush itself stays structural.
        # BSP recovery does not know these lost authoring flags. Keep a control
        # that isolates that loss from the ordering feature being tested.
        reference = original
        if mode == "structural-override":
            control = directory / "without-conflicting-flags.map"
            control.write_text(text.replace(" 134217728 0 0\n", " 0 0 0\n"), encoding="utf-8")
            reference = compile_map(control, mode+"-control")
            assert geometry_signature(reference, ordered=True) == original_geometry
            assert spatial_signature(reference, points)[1] != signature[1]
        reference_signature = spatial_signature(reference, points)
        reference_nodes = reference.summary()["nodes"]

        # An explicit default must have byte-identical MAP content, and the legacy
        # conversion interface must select the same policy as -decompile.
        default = directory / "default.map"
        explicit = directory / "explicit-bsp.map"
        run(exe, [*base, "-decompile", "-o", default, source.with_suffix(".bsp")], root, mode+"-default")
        run(exe, [*base, "-convert", "-format", "map_220", "-brush-order", "bsp", "-o", explicit,
                  "-report", directory/"explicit-bsp.json", source.with_suffix(".bsp")], root, mode+"-explicit-bsp")
        assert default.read_bytes() == explicit.read_bytes()
        policy = json.loads((directory/"explicit-bsp.json").read_text())["brush_order"]
        assert policy == {"policy": "bsp", "basis": "bsp_brush_record_order", "author_order_proven": False,
                          "rebuild_equivalence_proven": False}
        for fast in (False, True):
            for fmt in ("map", "map_bp", "map_220"):
                label = f'{mode}-{"fast" if fast else "full"}-{fmt}'
                output = directory / (label+".map")
                options = ["-fast"] if fast else []
                run(exe, [*base, "-decompile", *options, "-format", fmt, "-brush-order", "rebuild", "-o", output,
                          source.with_suffix(".bsp")], root, label)
                report = json.loads(Path(str(output)+".recovery.json").read_text())
                assert report["brush_order"] == {"policy": "rebuild", "basis": "q3mapx_map_loader_side_shader_opacity",
                                                  "author_order_proven": False, "rebuild_equivalence_proven": False}
                rebuilt = compile_map(output, label)
                assert geometry_signature(rebuilt, ordered=True) == original_geometry, label+" changed compiled brush order or geometry"
                observed = spatial_signature(rebuilt, points)
                assert observed == reference_signature, label+" changed sampled partitions or visibility"
                assert rebuilt.summary()["nodes"] == reference_nodes, label+" changed BSP node count"
                assert b'"targetname" "test_door"' in rebuilt.lump(0)
                # Only worldspawn's compiler-command history should change.
                assert rebuilt.lump(0).split(b'}', 1)[1] == original.lump(0).split(b'}', 1)[1]
                checks.append({"case": mode, "fast": fast, "format": fmt,
                               "source_sha256": hashlib.sha256(original.data).hexdigest(),
                               "source_nodes": source_nodes, "rebuilt_nodes": rebuilt.summary()["nodes"],
                               "compiled_brush_order_geometry_materials_contents_match": True,
                               "opaque_space_matches_source": observed[0] == signature[0],
                               "partition_pair_differences_from_source": sum(a != b for a, b in zip(signature[1], observed[1])),
                               "pvs_pairs_added_to_source": sum(not a and b for a, b in zip(signature[2], observed[2])),
                               "pvs_pairs_removed_from_source": sum(a and not b for a, b in zip(signature[2], observed[2])),
                               "matches_control_without_conflicting_detail_flags": True if mode == "structural-override" else None})
        assert source.read_bytes() == source_bytes and source.with_suffix(".bsp").read_bytes() == original.data

    # Exercise both translucent side classification and multiple opaque brushes at
    # another worker count. This checks deterministic output, not source identity.
    directory = root / "mixed-opacity"
    output = directory / "workers4.map"
    base = ["-game", "quake3", "-fs_basepath", directory, "-fs_homepath", root/"home", "-threads", 4]
    run(exe, [*base, "-decompile", "-brush-order", "rebuild", "-o", output,
              directory/"baseq3/maps/fixture.bsp"], root, "workers4")
    assert output.read_bytes() == (directory/"mixed-opacity-full-map_220.map").read_bytes()

    # A valid but redundant side can carry a translucent shader. It has no face
    # in the recovered MAP, so it must not alter that brush's loader insertion.
    original = Bsp(directory/"baseq3/maps/fixture.bsp")
    payloads = [original.lump(i) for i in range(17)]
    brushes = list(struct.iter_unpack("<3i", payloads[8]))
    sides = list(struct.iter_unpack("<2i", payloads[9]))
    shaders = list(struct.iter_unpack("<64s2i", payloads[1]))
    material = next(i for i, shader in enumerate(shaders) if shader[0].split(b'\0')[0] == b'textures/q3mapx/structural')
    first, size, shader = brushes[0]
    assert all(shaders[s[1]][0].split(b'\0')[0] == b'textures/q3mapx/stone' for s in sides[first:first+size])
    payloads[2] += struct.pack("<8f", 1, 0, 0, 4096, -1, 0, 0, -4096)
    payloads[9] += payloads[9][first*8:(first+size)*8] + struct.pack("<2i", len(original.lump(2))//16, material)
    payloads[8] = struct.pack("<3i", len(sides), size+1, shader) + payloads[8][12:]
    redundant = directory/"redundant-translucent-side.bsp"
    redundant.write_bytes(pack_file(46, payloads))
    redundant_hash = hashlib.sha256(redundant.read_bytes()).hexdigest()
    redundant_checks = []
    for fast in (False, True):
        label = "redundant-side-" + ("fast" if fast else "full")
        output = directory/(label+".map")
        run(exe, [*base, "-decompile", *(["-fast"] if fast else []), "-brush-order", "rebuild", "-o", output, redundant], root, label)
        # compile_map is tied to the last corpus case; spell out the asset paths
        # here because this independent control uses the mixed-opacity materials.
        run(exe, [*base, "-meta", output], root, label+"-bsp")
        run(exe, [*base, "-vis", "-reproducible", "-saveprt", output], root, label+"-vis")
        rebuilt = Bsp(output.with_suffix(".bsp"))
        assert geometry_signature(rebuilt, ordered=True) == geometry_signature(original, ordered=True)
        assert spatial_signature(rebuilt, points) == spatial_signature(original, points)
        assert not output.with_suffix(".lin").exists()
        redundant_checks.append({"fast": fast, "input_sha256": redundant_hash,
                                 "compiled_brush_order_and_spatial_relationships_match": True})
    assert hashlib.sha256(redundant.read_bytes()).hexdigest() == redundant_hash

    invalid = [(["-decompile", "-brush-order", "reverse"], "Brush order must be"),
               (["-decompile", "-brush-order"], "-brush-order"),
               (["-convert", "-format", "obj", "-brush-order", "bsp"], "requires map"),
               (["-convert", "-format", "ase", "-brush-order", "rebuild"], "requires map"),
               (["-convert", "-format", "wolf", "-brush-order", "rebuild"], "requires map"),
               (["-decompile", "-brush-order", "rebuild", "-wtf"], "cannot be combined")]
    invalid += [(["-game", game, "-decompile", "-brush-order", "rebuild"], "BSP-writing game profile")
                for game in ("q3-ihv", "q3test44", "q3test45", "alice", "fakk2", "mohaa")]
    for i, (options, message) in enumerate(invalid):
        result = subprocess.run([str(exe), *options, str(directory/"baseq3/maps/fixture.bsp")], cwd=root,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        text = result.stdout.decode("utf-8", errors="replace")
        (root/f"invalid-{i}.log").write_text(text, encoding="utf-8")
        assert result.returncode == 1 and message in text, (options, result.returncode, text)
        assert "Loading " not in text
    (root/"validation.json").write_text(json.dumps({"schema_version": 1, "round_trips": checks,
        "redundant_side_round_trips": redundant_checks,
        "spatial_samples": len(points), "ordered_sample_pairs": len(points)**2,
        "default_and_legacy_byte_parity_cases": len(cases), "worker_parity": [1, 4],
        "invalid_options": len(invalid), "sources_preserved": True,
        "scope": "Generated axial Quake III geometry and finite spatial samples; not a proof of general rebuild equivalence"}, indent=2)+"\n")
    print("38 rebuild-order round trips passed; source ordering, geometry, sampled PVS, default/worker parity, redundant sides and lost-flag control checked")


if __name__ == "__main__":
    main()

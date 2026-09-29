"""Texture recovery and report regressions. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
import json
from pathlib import Path
import re
import struct

from fixtures import create_fixture
from integration import Bsp, check_uvs, run


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--legacy", action="store_true", help="Exercise the regression against imported NRC")
    args = parser.parse_args()
    exe, directory = args.compiler.resolve(strict=True), args.work_dir.resolve()
    source = create_fixture(directory)
    base = ["-game", "quake3", "-fs_basepath", directory, "-threads", 2]
    run(exe, [*base, "-meta", source], directory, "compile")
    original = Bsp(source.with_suffix(".bsp"))
    changed = bytearray(original.data)
    surface_offset = vertex_start = vertex_count = None
    for i in range(original.summary()["surfaces"]):
        offset = original.lumps[13][0] + i * 104
        shader, fog, kind, first, count = struct.unpack_from("<5i", changed, offset)
        voffset = original.lumps[10][0] + first * 44
        xyz = struct.unpack_from("<3f", changed, voffset)
        normal = struct.unpack_from("<3f", changed, voffset + 28)
        if kind == 1 and abs(xyz[2] - 96) < 0.01 and normal[2] > 0.99:
            surface_offset, vertex_start, vertex_count = offset, first, count
            break
    assert surface_offset is not None, "Missing pillar top in fixture"
    offsets = [original.lumps[10][0] + (vertex_start + i) * 44 for i in range(vertex_count)]
    verts = [struct.unpack_from("<5f", changed, offset) for offset in offsets]
    x0, y0, _, u0, v0 = verts[0]
    dx1, dy1 = verts[1][0] - x0, verts[1][1] - y0
    dx2, dy2 = verts[2][0] - x0, verts[2][1] - y0
    determinant = dx1 * dy2 - dx2 * dy1
    assert determinant != 0
    gradients = []
    for component in (3, 4):
        d1 = verts[1][component] - verts[0][component]
        d2 = verts[2][component] - verts[0][component]
        gradients.append(((d1 * dy2 - d2 * dy1) / determinant, (dx1 * d2 - dx2 * d1) / determinant))
    for offset, vertex in zip(offsets, verts):
        x, y = vertex[0] * 16, vertex[1] * 16
        uv = [verts[0][axis + 3] + gradients[axis][0] * (x - x0) + gradients[axis][1] * (y - y0) for axis in range(2)]
        struct.pack_into("<5f", changed, offset, x, y, vertex[2], *uv)
    large = source.with_name("large-overlap.bsp")
    large.write_bytes(changed)
    run(exe, [*base, "-convert", "-format", "map_220", large], directory, "recover-overlap")
    recovered = large.with_name("large-overlap_converted.map")
    run(exe, [*base, "-meta", recovered], directory, "recompile-overlap")
    check_uvs(original, Bsp(recovered.with_suffix(".bsp")))
    if args.legacy:
        return

    output = directory / "recovered project.map"
    run(exe, [*base, "-decompile", "-o", output, large], directory, "decompile-alias")
    report = json.loads(Path(str(output) + ".recovery.json").read_text())
    assert report["schema_version"] == 1 and report["format"] == "map_220"
    assert report["brushes"] == 8 and report["patches"] == 1
    assert report["matched_uv_faces"] + report["fallback_uv_faces"] == report["faces"]
    assert report["degenerate_uv_transforms"] == 0
    assert report["limitations"] and report["output"] == str(output)

    # Constant UVs used to cause division by zero in Valve 220 export.
    for offset in offsets:
        struct.pack_into("<2f", changed, offset + 12, 0, 0)
    degenerate = source.with_name("degenerate-uv.bsp")
    degenerate.write_bytes(changed)
    run(exe, [*base, "-decompile", degenerate], directory, "degenerate-uv")
    result_map = degenerate.with_name("degenerate-uv_converted.map")
    assert not re.search(r"(?<!\w)[+-]?(nan|inf)(?!\w)", result_map.read_text(), re.I)
    report = json.loads(Path(str(result_map) + ".recovery.json").read_text())
    assert report["degenerate_uv_transforms"] > 0
    run(exe, [*base, "-meta", result_map], directory, "recompile-degenerate")
    print("Large overlapping triangle UV recovery, decompile options, reports and finite fallback passed")


if __name__ == "__main__":
    main()

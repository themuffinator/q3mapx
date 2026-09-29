"""Real CLI regression pipeline; no installed game or input automation required.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import re
from pathlib import Path
import struct
import subprocess
import time

from fixtures import create_fixture


class Bsp:
    def __init__(self, path):
        self.data = Path(path).read_bytes()
        assert self.data[:4] == b"IBSP", "Expected IBSP output"
        assert struct.unpack_from("<i", self.data, 4)[0] == 46
        self.lumps = [struct.unpack_from("<ii", self.data, 8 + i * 8) for i in range(17)]
        for offset, size in self.lumps:
            assert offset >= 0 and size >= 0 and offset + size <= len(self.data)

    def lump(self, number):
        offset, size = self.lumps[number]
        return self.data[offset:offset + size]

    def summary(self):
        sizes = {"shaders": (1, 72), "planes": (2, 16), "nodes": (3, 36),
                 "leaves": (4, 48), "models": (7, 40), "brushes": (8, 12),
                 "sides": (9, 8), "vertices": (10, 44), "indices": (11, 4),
                 "surfaces": (13, 104)}
        return {name: len(self.lump(lump)) // stride for name, (lump, stride) in sizes.items()}


def run(exe, args, directory, label, timeout=90):
    start = time.perf_counter()
    result = subprocess.run([str(exe), *map(str, args)], cwd=directory,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
    elapsed = time.perf_counter() - start
    output = result.stdout.decode("utf-8", errors="replace")
    (directory / f"{label}.log").write_text(output, encoding="utf-8")
    if result.returncode:
        raise AssertionError(f"{label}: exit {result.returncode}\n{output[-8000:]}")
    return {"stage": label, "seconds": elapsed, "arguments": list(map(str, args))}


def vertex_uvs(bsp):
    data = bsp.lump(10)
    # Position and normal distinguish intersecting planar faces. Texture shifts are periodic.
    return {tuple(round(v, 2) for v in struct.unpack_from("<3f", data, offset))
            + tuple(round(v, 2) for v in struct.unpack_from("<3f", data, offset + 28)):
            struct.unpack_from("<2f", data, offset + 12) for offset in range(0, len(data), 44)}


def check_uvs(original, recovered):
    a, b = vertex_uvs(original), vertex_uvs(recovered)
    common = a.keys() & b.keys()
    assert len(common) >= len(a) * 0.9, "Recovered mesh lost too many fixture vertices"
    for key in common:
        error = max(abs((a[key][axis] - b[key][axis] + 0.5) % 1 - 0.5) for axis in range(2))
        assert error < 0.001, f"Texture alignment changed at {key}: {a[key]} vs {b[key]}"


def pipeline(exe, root, threads):
    directory = root / f"pipeline-{threads}"
    source = create_fixture(directory)
    base = ["-game", "quake3", "-fs_basepath", directory, "-threads", threads]
    measurements = []
    for label, options in (("bsp", ["-meta"]), ("vis", ["-vis"]),
                           ("light", ["-light", "-fast", "-samples", 2])):
        measurements.append(run(exe, [*base, *options, source], directory, label))
    compiled = Bsp(source.with_suffix(".bsp"))
    counts = compiled.summary()
    assert counts["models"] == 2, counts
    assert counts["brushes"] == 8, counts
    assert counts["surfaces"] > 0 and counts["vertices"] > 0, counts
    assert b'"targetname" "test_door"' in compiled.lump(0)
    assert len(compiled.lump(14)) > 0, "No baked lightmaps"
    assert len(compiled.lump(16)) > 8, "No visibility data"
    for mapformat in ("map", "map_bp", "map_220"):
        measurements.append(run(exe, [*base, "-convert", "-format", mapformat, source.with_suffix(".bsp")],
                                directory, f"decompile-{mapformat}"))
        recovered = source.with_name("fixture_converted.map")
        text = recovered.read_text(encoding="utf-8")
        assert "patchDef2" in text and '"test_door"' in text
        assert not re.search(r"(?<!\w)[+-]?(nan|inf)(?!\w)", text, re.I), "Non-finite recovered coordinates"
        measurements.append(run(exe, [*base, "-meta", recovered], directory, f"recompile-{mapformat}"))
        rebuilt = Bsp(recovered.with_suffix(".bsp"))
        assert rebuilt.summary()["models"] == counts["models"]
        assert rebuilt.summary()["brushes"] == counts["brushes"]
        assert b'"targetname" "test_door"' in rebuilt.lump(0)
        check_uvs(compiled, rebuilt)
        # Save each recovered text for diagnosis; subsequent formats use the same legacy output name.
        (directory / f"recovered-{mapformat}.map").write_text(text, encoding="utf-8")
    minimap = directory / "minimap.tga"
    measurements.append(run(exe, [*base, "-minimap", "-size", 64, "-o", minimap, source.with_suffix(".bsp")],
                            directory, "minimap"))
    assert struct.unpack_from("<HH", minimap.read_bytes(), 12) == (64, 64)
    return {"threads": threads, "counts": counts,
            "geometry_sha256": hashlib.sha256(compiled.lump(2) + compiled.lump(8) + compiled.lump(9)).hexdigest(),
            "measurements": measurements}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    args.work_dir = args.work_dir.resolve()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    results = [pipeline(args.compiler.resolve(), args.work_dir, threads) for threads in (1, 4)]
    assert results[0]["geometry_sha256"] == results[1]["geometry_sha256"], "Thread-dependent brush geometry"
    (args.work_dir / "report.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print("BSP/VIS/LIGHT, three decompile formats, recompilation, minimap and thread parity passed")


if __name__ == "__main__":
    main()

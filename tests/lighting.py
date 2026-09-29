"""Material-aware lighting regressions. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from fixtures import create_lighting_fixture
from integration import Bsp, run


LIGHT_LUMPS = {10: "vertices", 13: "surfaces", 14: "lightmaps", 15: "lightgrid"}
MODES = {
    "adaptive": ["-fast", "-samples", 2],
    "bounce": ["-fast", "-samples", 2, "-bounce", 1],
    "deluxe": ["-fast", "-samples", 2, "-deluxe", "-bounce", 1],
    "supersampled": ["-fast", "-super", 2, "-samples", 2],
    "random": ["-fast", "-samples", "+4", "-randomsamples", "-bounce", 1],
    "dirt": ["-fast", "-dirty", "-dirtmode", 1],
    "flood": ["-fast", "-samples", 2, "-floodlight", "-lowquality"],
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    root = args.work_dir.resolve()
    source = create_lighting_fixture(root)
    exe = args.compiler.resolve()
    reference = args.reference.resolve() if args.reference else exe
    base = ["-game", "quake3", "-fs_basepath", root]
    run(reference, [*base, "-threads", 1, "-meta", source], root, "bsp")
    run(reference, [*base, "-threads", 1, "-vis", source], root, "vis")
    bsp_path = source.with_suffix(".bsp")
    original = bsp_path.read_bytes()
    records = []
    for name, options in MODES.items():
        expected = None
        for label, compiler, threads in (("reference", reference, 1), ("single", exe, 1), ("parallel", exe, 4), ("unculled", exe, 4)):
            bsp_path.write_bytes(original)
            culling = ["-no-light-culling"] if label == "unculled" else []
            timing = run(compiler, [*base, "-threads", threads, "-light", *options, *culling, source],
                         root, f"{name}-{label}", timeout=180)
            result = Bsp(bsp_path)
            assert result.lump(14) and max(result.lump(14)) > 0, "Empty/black lightmaps"
            actual = {number: result.lump(number) for number in LIGHT_LUMPS}
            if expected is None:
                expected = actual
                (root / f"{name}-reference.bsp").write_bytes(result.data)
            for number, value in actual.items():
                if value != expected[number]:
                    (root / f"{name}-{label}-mismatch.bsp").write_bytes(result.data)
                    differences = [i for i, (a, b) in enumerate(zip(value, expected[number])) if a != b]
                    raise AssertionError(f"{name}/{label}: {LIGHT_LUMPS[number]} changed; {len(differences)} bytes at {differences[:16]}")
            records.append({"mode": name, "run": label, "threads": threads, **timing,
                            "sha256": {LIGHT_LUMPS[n]: hashlib.sha256(v).hexdigest() for n, v in actual.items()}})
    # Prove the corpus observes actual shadow tracing, not merely loading shaders.
    bsp_path.write_bytes(original)
    run(exe, [*base, "-threads", 1, "-light", *MODES["adaptive"], "-notrace", source], root, "unshadowed")
    unshadowed = hashlib.sha256(Bsp(bsp_path).lump(14)).hexdigest()
    assert unshadowed != records[0]["sha256"]["lightmaps"], "Fixture did not exercise shadow tracing"
    flood = next(r for r in records if r["mode"] == "flood")
    assert flood["sha256"]["lightmaps"] != records[0]["sha256"]["lightmaps"], "Low-quality floodlight had no effect"
    # Independently change alpha and RGB texels while retaining the same geometry.
    # These checks catch fixtures whose material flags or image lookup are ineffective.
    for texture, channels in (("fence", (3,)), ("filter", (0, 1, 2))):
        image = source.parent.parent / f"textures/q3mapx/{texture}.tga"
        saved = image.read_bytes()
        changed = bytearray(saved)
        for offset in range(18, len(changed), 4):
            for channel in channels:
                changed[offset + channel] = 0 if texture == "fence" else 255
        try:
            image.write_bytes(changed)
            bsp_path.write_bytes(original)
            run(exe, [*base, "-threads", 1, "-light", *MODES["adaptive"], source], root, f"clear-{texture}")
            digest = hashlib.sha256(Bsp(bsp_path).lump(14)).hexdigest()
            assert digest != records[0]["sha256"]["lightmaps"], f"Fixture did not exercise {texture} texture filtering"
        finally:
            image.write_bytes(saved)
    # Dense geometry puts grid points inside solid brushes and exercises randomized
    # escape nudges, as well as many parallel bounce emitters and culling counters.
    dense = create_lighting_fixture(root / "dense", dense=True, grid=5)
    dense_root = root / "dense"
    dense_base = ["-game", "quake3", "-fs_basepath", dense_root, "-v"]
    run(exe, [*dense_base, "-threads", 1, "-meta", dense], dense_root, "bsp")
    run(exe, [*dense_base, "-threads", 1, "-vis", "-fast", dense], dense_root, "vis")
    dense_bsp = dense.with_suffix(".bsp")
    unlit = dense_bsp.read_bytes()
    expected = statistics = None
    for threads in (1, 4, 20, 70):
        dense_bsp.write_bytes(unlit)
        label = f"dense-{threads}"
        run(exe, [*dense_base, "-threads", threads, "-light", "-fast", "-samples", "+4",
                  "-randomsamples", "-bounce", 1, "-bouncegrid", dense], dense_root, label, timeout=180)
        result = Bsp(dense_bsp)
        actual = {n: result.lump(n) for n in LIGHT_LUMPS}
        log = (dense_root / f"{label}.log").read_text(encoding="utf-8")
        counts = re.findall(r"^\s*(\d+) (?:grid points .*culled|lights .*culled|luxels (?:mapped|occluded|illuminated)|vertexes illuminated|diffuse surfaces|total diffuse lights)\s*$", log, re.M)
        assert len(counts) >= 12, "Lighting statistics missing"
        if expected is None:
            expected, statistics = actual, counts
        assert actual == expected, f"Dense lighting output differs at {threads} workers"
        assert counts == statistics, f"Lighting counters differ at {threads} workers"
    (root / "lighting-report.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    print("Material shadows, sun/sky, bounce, deluxe, random/dirt/flood sampling, dense grid and 1/4/20/70-worker parity passed")


if __name__ == "__main__":
    main()

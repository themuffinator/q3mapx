"""Hybrid GPU lighting/material parity and explicit failure behavior. GPL-3.0-or-later."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from fixtures import create_lighting_fixture
from integration import Bsp, run
from lighting import LIGHT_LUMPS, MODES


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    source = create_lighting_fixture(root)
    base = ["-game", "quake3", "-fs_basepath", root]
    run(exe, [*base, "-threads", 1, "-meta", source], root, "bsp")
    run(exe, [*base, "-threads", 1, "-vis", source], root, "vis")
    bsp_path = source.with_suffix(".bsp")
    original = bsp_path.read_bytes()
    command = [str(exe), *map(str, base), "-threads", "4", "-light", "-fast", "-light-backend", "gpu", str(source)]
    disabled = subprocess.run(command, cwd=root, env=dict(os.environ, Q3MAPX_DISABLE_GPU="1"),
                              capture_output=True, timeout=60)
    (root / "disabled.log").write_bytes(disabled.stdout + disabled.stderr)
    assert disabled.returncode != 0 and b"GPU lighting initialization failed" in disabled.stdout
    assert bsp_path.read_bytes() == original, "Failed GPU startup modified the BSP"
    # Probe automatic selection. Missing runtime/FP64 are legitimate CI skips;
    # a shader compilation, dispatch, memory or other failure is a regression.
    probe = subprocess.run(command, cwd=root, capture_output=True, timeout=120)
    (root / "probe.log").write_bytes(probe.stdout + probe.stderr)
    if probe.returncode:
        allowed = (b"OpenCL disabled at build time", b"OpenCL runtime unavailable", b"No usable OpenCL",
                   b"GPU area factors require double precision", b"Finding OpenCL platforms (OpenCL -1001)")
        assert any(reason in probe.stdout for reason in allowed), probe.stdout[-4000:]
        print("No suitable OpenCL FP64 GPU: failure behavior passed; hardware bake checks skipped")
        return
    records = []
    modes = {**MODES, "accurate": ["-samples", 2, "-bounce", 1], "faster": ["-faster", "-samples", 2, "-bounce", 1]}
    for name, options in modes.items():
        expected = None
        for backend, threads in (("cpu", 1), ("gpu", 1), ("gpu", 4)):
            bsp_path.write_bytes(original)
            label = f"{name}-{backend}-{threads}"
            reportpath = root / f"{label}.json"
            timing = run(exe, [*base, "-threads", threads, "-light", *options, "-light-backend", backend,
                               "-compute-report", reportpath, source], root, label, timeout=180)
            actual = Bsp(bsp_path)
            output = {n: actual.lump(n) for n in LIGHT_LUMPS}
            if expected is None:
                expected = output
            for n, value in output.items():
                assert value == expected[n], f"{label}: {LIGHT_LUMPS[n]} differs"
            report = json.loads(reportpath.read_text())
            if backend == "gpu" and name != "faster":
                assert report["backend"] == "hybrid" and report["batches"] > 0 and report["factors"] > 0, report
                assert report["kernel_seconds"] > 0 and report["fallback_batches"] == 0, report
            else:
                assert report["backend"] == "cpu" and report["batches"] == 0, report
            records.append({"mode": name, "backend": backend, "threads": threads, "compute": report, **timing})
    # Exercise streamed light groups: the large floor's complete matrix exceeds
    # the 64 MiB cache, so one-pass caching would have fallen back to CPU here.
    dense = root / "dense"
    dense_source = create_lighting_fixture(dense, dense=True, grid=21)
    dense_base = ["-game", "quake3", "-fs_basepath", dense, "-threads", 20]
    run(exe, [*dense_base, "-meta", dense_source], dense, "bsp")
    run(exe, [*dense_base, "-vis", "-fast", dense_source], dense, "vis")
    dense_bsp = dense_source.with_suffix(".bsp")
    unlit = dense_bsp.read_bytes()
    expected = None
    for backend in ("cpu", "gpu"):
        dense_bsp.write_bytes(unlit)
        path = dense / f"{backend}.json"
        timing = run(exe, [*dense_base, "-light", "-fast", "-samples", 4, "-bounce", 1,
                          "-light-backend", backend, "-compute-report", path, dense_source], dense, backend, timeout=180)
        result = Bsp(dense_bsp)
        output = {n: result.lump(n) for n in LIGHT_LUMPS}
        if expected is None:
            expected = output
        assert output == expected, "Streamed area-light results differ from CPU"
        report = json.loads(path.read_text())
        if backend == "gpu":
            assert report["batches"] > 8 and report["fallback_batches"] == 0, report
        records.append({"mode": "streamed", "backend": backend, "threads": 20, "compute": report, **timing})
    (root / "hardware-parity.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    print("Hybrid lighting: exact CPU/GPU material bakes at 1/4 workers and missing-runtime checks passed")


if __name__ == "__main__":
    main()

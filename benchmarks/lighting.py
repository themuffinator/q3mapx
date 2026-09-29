"""Alternating complete lighting passes with material/output checks. GPL-3.0-or-later."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from fixtures import create_lighting_fixture
from integration import Bsp, run
from lighting import LIGHT_LUMPS


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--grid", type=int, default=21)
    parser.add_argument("--threads", type=int, nargs="+", default=[1, 20])
    parser.add_argument("--repeat", type=int, default=5)
    parser.add_argument("--samples", type=int, default=4)
    parser.add_argument("--bounce", type=int, default=1)
    parser.add_argument("--no-grid-lighting", action="store_true", help="Isolate surface lighting when comparing legacy nondeterministic grid sampling")
    args = parser.parse_args()
    if args.repeat < 2 or not 3 <= args.grid <= 63 or args.grid % 2 != 1:
        parser.error("Use at least two measured runs and an odd grid size in 3..63")
    root = args.work_dir.resolve()
    source = create_lighting_fixture(root, dense=True, grid=args.grid)
    base = ["-game", "quake3", "-fs_basepath", root]
    methods = [("baseline", args.baseline.resolve()), ("q3mapx", args.compiler.resolve())]
    hashes = {name: hashlib.sha256(exe.read_bytes()).hexdigest() for name, exe in methods}
    run(methods[0][1], [*base, "-threads", 1, "-meta", source], root, "bsp", timeout=300)
    run(methods[0][1], [*base, "-threads", 1, "-vis", "-fast", source], root, "vis", timeout=300)
    bsp_path = source.with_suffix(".bsp")
    original = bsp_path.read_bytes()
    records = []
    options = ["-light", "-fast", "-samples", args.samples, "-bounce", args.bounce]
    if args.no_grid_lighting:
        options.append("-nogrid")
    for threads in args.threads:
        expected = None
        for iteration in range(args.repeat + 1):
            for name, exe in methods[iteration % 2:] + methods[:iteration % 2]:
                label = f"{name}-{threads}-{iteration}"
                bsp_path.write_bytes(original)
                profile_path = root / f"{label}.json"
                result = run(exe, [*base, "-threads", threads, "-profile", profile_path, *options, source],
                             root, label, timeout=600)
                bsp = Bsp(bsp_path)
                output = {n: bsp.lump(n) for n in LIGHT_LUMPS}
                if expected is None:
                    expected = output
                for n, data in output.items():
                    assert data == expected[n], f"{label}: {LIGHT_LUMPS[n]} differ"
                records.append({"implementation": name, "threads": threads, "warmup": iteration == 0,
                                **result, "profile": json.loads(profile_path.read_text()),
                                "sha256": {LIGHT_LUMPS[n]: hashlib.sha256(v).hexdigest() for n, v in output.items()}})
                print(label, f"{result['seconds']:.4f}s", "exact output", flush=True)
    for name, exe in methods:
        assert hashlib.sha256(exe.read_bytes()).hexdigest() == hashes[name], f"{exe} changed during measurement"
    summaries = []
    for threads in args.threads:
        for name, exe in methods:
            times = [r["seconds"] for r in records if r["implementation"] == name and r["threads"] == threads and not r["warmup"]]
            summaries.append({"implementation": name, "compiler": str(exe), "compiler_sha256": hashes[name],
                              "threads": threads, "seconds": times, "median_seconds": statistics.median(times)})
    report = {"schema_version": 1, "kind": "whole_process_material_lighting",
              "timestamp_utc": datetime.now(timezone.utc).isoformat(), "platform": platform.platform(),
              "processor": platform.processor(), "logical_cpus": os.cpu_count(), "fixture_grid": args.grid,
              "fixture_sha256": hashlib.sha256(source.read_bytes()).hexdigest(), "options": options,
              "visibility_mode": "fast", "warmup_runs": 1, "measured_runs": args.repeat,
              "summaries": summaries, "records": records}
    (root / "benchmark.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summaries, indent=2))


if __name__ == "__main__":
    main()

"""Repeatable end-to-end compiler benchmark. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
from datetime import datetime, timezone

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from fixtures import create_fixture
from integration import Bsp, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--threads", type=int, nargs="+", default=[1, 4])
    parser.add_argument("--repeat", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--label", default="working-tree")
    args = parser.parse_args()
    if args.repeat < 2 or args.warmup < 0 or any(t < 1 for t in args.threads):
        parser.error("Use at least two measured runs, nonnegative warmups, and positive thread counts")
    exe = args.compiler.resolve(strict=True)
    root = args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    records = []
    for threads in args.threads:
        directory = root / f"threads-{threads}"
        source = create_fixture(directory, dense=True)
        bsp = source.with_suffix(".bsp")
        base = ["-game", "quake3", "-fs_basepath", directory, "-threads", threads]
        stages = [("bsp", ["-meta", source]), ("vis", ["-vis", "-saveprt", bsp]),
                  ("light", ["-light", "-fast", "-samples", 2, bsp]),
                  ("decompile", ["-convert", "-format", "map_220", bsp]),
                  ("minimap", ["-minimap", "-size", 512, "-samples", 4, bsp])]
        for stage, options in stages:
            timings = []
            for iteration in range(args.warmup + args.repeat):
                result = run(exe, [*base, *options], directory, f"{stage}-{iteration}", timeout=300)
                if iteration >= args.warmup:
                    timings.append(result["seconds"])
            compiled = Bsp(bsp)
            record = {"stage": stage, "threads": threads,
                      "seconds": timings, "median_seconds": statistics.median(timings),
                      "min_seconds": min(timings), "max_seconds": max(timings),
                      "arguments": list(map(str, [*base, *options])),
                      "geometry_sha256": hashlib.sha256(b"".join(compiled.lump(i) for i in (2, 8, 9))).hexdigest(),
                      "counts": compiled.summary()}
            records.append(record)
            print(f"{stage:12} {threads:3} threads: {record['median_seconds']:.4f}s "
                  f"[{min(timings):.4f}, {max(timings):.4f}]", flush=True)
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=Path(__file__).resolve().parents[1],
                              capture_output=True, text=True, check=True).stdout.strip()
    document = {"schema_version": 1, "label": args.label, "harness_revision": revision,
                "timestamp_utc": datetime.now(timezone.utc).isoformat(),
                "compiler": str(exe), "compiler_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
                "platform": platform.platform(), "processor": platform.processor(),
                "logical_cpus": os.cpu_count(), "warmup_runs": args.warmup,
                "measured_runs": args.repeat, "fixture": "generated-dense-room-v1",
                "records": records}
    report = root / "benchmark.json"
    report.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    print(report)


if __name__ == "__main__":
    main()

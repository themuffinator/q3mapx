"""Whole-command Raven BSP rewrite benchmark; SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from fixtures import create_fixture
from integration import run
from lightgrid_cli import lumps, with_grid


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--map", type=Path, help="Optional existing RBSP; only a private copy is rewritten")
    parser.add_argument("--repeat", type=int, default=5)
    parser.add_argument("--points", type=int, default=24000)
    args = parser.parse_args()
    if args.repeat < 2 or not 1 <= args.points <= 65535:
        parser.error("Use at least two measured runs and 1..65535 dictionary points")
    root = args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    methods = [("baseline", args.baseline.resolve()), ("q3mapx", args.compiler.resolve())]
    hashes = {name: hashlib.sha256(exe.read_bytes()).hexdigest() for name, exe in methods}
    base = ["-game", "ja", "-fs_basepath", root, "-fs_basegame", "baseq3", "-threads", 1]
    if args.map:
        original = args.map.resolve(strict=True).read_bytes()
        kind = "local_native_map"
    else:
        source = create_fixture(root)
        run(methods[0][1], [*base, "-meta", source], root, "prepare")
        randomizer = random.Random(0x52425350)
        palette = [bytes(randomizer.randrange(256) for _ in range(24)) + b"\0\xfe\xfe\xfe"
                   + bytes(randomizer.randrange(256) for _ in range(2)) for _ in range(args.points)]
        indices = list(range(args.points)) * 4
        original = with_grid(source.with_suffix(".bsp").read_bytes(), palette, indices)
        kind = "synthetic_high_variation_grid"
    path = root / "input.bsp"
    path.write_bytes(original)
    expected = None
    records = []
    for iteration in range(args.repeat + 1):
        for name, exe in methods[iteration % 2:] + methods[:iteration % 2]:
            result = run(exe, [*base, "-scale", 1, path], root, f"{name}-{iteration}", timeout=300)
            output = lumps((root / "input_s.bsp").read_bytes())
            if expected is None:
                expected = output
            assert output == expected, f"{name}: BSP lump payloads differ"
            records.append({"implementation": name, "warmup": iteration == 0, **result})
            print(name, iteration, f"{result['seconds']:.4f}s", "all 18 lumps identical", flush=True)
    assert path.read_bytes() == original
    for name, exe in methods:
        assert hashlib.sha256(exe.read_bytes()).hexdigest() == hashes[name], "Executable changed during benchmark"
    summaries = []
    for name, exe in methods:
        times = [r["seconds"] for r in records if r["implementation"] == name and not r["warmup"]]
        summaries.append({"implementation": name, "executable_sha256": hashes[name], "seconds": times,
                          "median_seconds": statistics.median(times)})
    payload = lumps(original)
    report = {"schema_version": 1, "kind": "whole_process_raven_grid_rewrite", "fixture_kind": kind,
              "timestamp_utc": datetime.now(timezone.utc).isoformat(), "platform": platform.platform(),
              "processor": platform.processor(), "logical_cpus": os.cpu_count(),
              "input_sha256": hashlib.sha256(original).hexdigest(), "dictionary_points": len(payload[15]) // 30,
              "grid_references": len(payload[17]) // 2, "warmup_runs": 1, "measured_runs": args.repeat,
              "output_lump_sha256": [hashlib.sha256(data).hexdigest() for data in expected],
              "summaries": summaries, "records": records}
    (root / "benchmark.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summaries, indent=2))


if __name__ == "__main__":
    main()

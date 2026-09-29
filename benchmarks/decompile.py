"""Alternate baseline/candidate runs on the same BSP. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import statistics
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
from fixtures import create_fixture
from integration import Bsp, run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--grid", type=int, default=31)
    parser.add_argument("--repeat", type=int, default=7)
    args = parser.parse_args()
    if args.grid < 3 or args.grid > 63 or args.grid % 2 == 0 or args.repeat < 3:
        parser.error("Use an odd grid of 3..63 and at least three repetitions")
    directory = args.work_dir.resolve()
    source = create_fixture(directory, dense=True, grid=args.grid)
    exes = {"baseline": args.baseline.resolve(strict=True), "candidate": args.candidate.resolve(strict=True)}
    base = ["-game", "quake3", "-fs_basepath", directory, "-threads", 1]
    run(exes["baseline"], [*base, "-meta", source], directory, "prepare", timeout=300)
    bsp = source.with_suffix(".bsp")
    samples = {key: [] for key in exes}
    for iteration in range(args.repeat + 2):
        order = ["baseline", "candidate"] if iteration % 2 == 0 else ["candidate", "baseline"]
        for name in order:
            measurement = run(exes[name], [*base, "-convert", "-format", "map_220", bsp], directory, f"{name}-{iteration}", timeout=300)
            if iteration >= 2:
                samples[name].append(measurement["seconds"])
    results = {name: {"compiler": str(exe), "sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
                      "seconds": samples[name], "median_seconds": statistics.median(samples[name])}
               for name, exe in exes.items()}
    report = {"schema_version": 1, "platform": platform.platform(), "fixture": f"dense-room-{args.grid}x{args.grid}",
              "bsp_sha256": hashlib.sha256(bsp.read_bytes()).hexdigest(), "counts": Bsp(bsp).summary(),
              "warmup_per_compiler": 2, "results": results}
    (directory / "comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    for name, result in results.items():
        print(f"{name}: {result['median_seconds']:.4f}s [{min(samples[name]):.4f}, {max(samples[name]):.4f}]")
    print(f"Median speedup: {results['baseline']['median_seconds'] / results['candidate']['median_seconds']:.2f}x")


if __name__ == "__main__":
    main()

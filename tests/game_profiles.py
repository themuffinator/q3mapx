"""Exercise every declared writable game profile. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from fixtures import create_fixture
from integration import run


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    executable_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
    root.mkdir(parents=True, exist_ok=True)
    query = subprocess.run([str(exe), "-games"], cwd=root, capture_output=True, check=True, timeout=15)
    catalog = json.loads(query.stdout)
    assert catalog["schema_version"] == 1 and catalog["compiler_version"]
    assert len(catalog["profiles"]) >= 19
    ids = [p["id"] for p in catalog["profiles"]]
    names = [name.casefold() for p in catalog["profiles"] for name in [p["id"], *p["aliases"]]]
    assert len(set(names)) == len(names)
    aliases = {a: p["id"] for p in catalog["profiles"] for a in p["aliases"]}
    expected = {"q3": "quake3", "ql": "quakelive", "rtcw-sp": "wolf", "rtcw-mp": "wolf",
                "wolfet": "et", "stvef-sp": "ef", "stvef-mp": "ef", "jk2-sp": "jk2",
                "jk2-mp": "jk2", "jka-sp": "ja", "jka-mp": "ja"}
    assert all(aliases.get(a) == profile for a, profile in expected.items())
    records = []
    for profile in catalog["profiles"]:
        if not profile["native_write"]:
            assert not set(profile["workflows"]) & {"build", "bsp", "vis", "light"}
            continue
        assert set(profile["workflows"]) >= {"build", "bsp", "vis", "light", "minimap", "decompile"}
        directory = root / profile["id"]
        source = create_fixture(directory, game_directory=profile["base_directory"],
                                shader_directory=profile["shader_directory"])
        base = ["-game", profile["id"], "-fs_basepath", directory,
                "-fs_homepath", directory / "home", "-threads", 2]
        for stage, options in (("bsp", ["-meta", "-leaktest"]), ("vis", ["-vis", "-saveprt"]),
                               ("light", ["-light", "-fast", "-samples", 1])):
            run(exe, [*base, *options, source], directory, stage)
        bsp = source.with_suffix(".bsp")
        data = bsp.read_bytes()
        assert data[:4].decode() == profile["bsp_ident"]
        assert struct.unpack_from("<i", data, 4)[0] == profile["bsp_version"]
        for number in (2, 3, 4, 7, 8, 9, 10, 13, 14, 15, 16):
            assert struct.unpack_from("<i", data, 8 + number * 8 + 4)[0] > 0, (profile["id"], number)
        output = directory / "recovered.map"
        run(exe, [*base, "-decompile", "-o", output, bsp], directory, "decompile")
        report = json.loads(Path(str(output) + ".recovery.json").read_text())
        assert report["brushes"] == 8 and report["patches"] == 1 and report["skipped_brushes"] == 0, report
        run(exe, [*base, "-meta", "-leaktest", output], directory, "round-trip")
        run(exe, [*base, "-minimap", "-backend", "cpu", "-size", 32, "-samples", 1,
                  "-o", directory / "minimap.tga", bsp], directory, "minimap")
        assert (directory / "minimap.tga").stat().st_size > 18
        for alias in profile["aliases"]:
            run(exe, ["-game", alias.upper(), *base[2:], "-info", bsp], directory, "alias-" + alias)
        records.append({"profile": profile["id"], "format": profile["bsp_ident"],
                        "version": profile["bsp_version"], "aliases": profile["aliases"],
                        "brushes": report["brushes"], "patches": report["patches"], "result": "passed"})
        print(profile["id"], "BSP/VIS/LIGHT/recovery/recompile/minimap passed", flush=True)
    # Unknown names must fail before replacing an existing BSP, never fall back
    # silently to Quake III or report success via the inherited April-fools path.
    source = root / "quake3/baseq3/maps/fixture.map"
    bsp = source.with_suffix(".bsp")
    original = bsp.read_bytes()
    for name in ("not-a-game", "quake1", "unreal"):
        result = subprocess.run([str(exe), "-game", name, "-fs_basepath", str(root), "-meta", str(source)],
                                cwd=root, capture_output=True, timeout=15)
        assert result.returncode == 1 and b"Unknown game profile" in result.stdout, result.stdout
        assert bsp.read_bytes() == original
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == executable_hash, "Compiler changed during profile matrix"
    (root / "validation.json").write_text(json.dumps({"schema_version": 1,
        "timestamp_utc": datetime.now(timezone.utc).isoformat(), "compiler_sha256": executable_hash,
        "compiler_version": catalog["compiler_version"], "profiles": records}, indent=2) + "\n")
    print(len(records), "writable profiles, aliases, full pipelines and invalid-name protection passed")


if __name__ == "__main__":
    main()

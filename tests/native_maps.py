"""Optional read-only installation coverage; never redistributes assets.

SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time
import zipfile


def selected_inputs(args):
    """Yield independent private inputs; never use archive names as output paths."""
    for source in [*(args.pak or []), *(args.bsp or [])]:
        path = source.resolve(strict=True)
        before = path.stat()
        if source in (args.bsp or []):
            if before.st_size > 1024**3:
                raise ValueError(f"Map exceeds the 1 GiB optional probe limit: {path.name}")
            yield None, path.name, path.read_bytes()
        else:
            with zipfile.ZipFile(path) as archive:
                for info in archive.infolist():
                    if not info.filename.lower().endswith(".bsp") or (args.map and info.filename not in args.map):
                        continue
                    if info.file_size > 1024**3:
                        raise ValueError(f"Map exceeds the 1 GiB optional probe limit: {info.filename}")
                    yield path.name, info.filename, archive.read(info)
        after = path.stat()
        assert (before.st_size, before.st_mtime_ns) == (after.st_size, after.st_mtime_ns), "Source changed"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--game", help="Required for native validation/recovery; optional explicit inspection profile")
    parser.add_argument("--pak", type=Path, action="append")
    parser.add_argument("--bsp", type=Path, action="append", help="Read-only loose BSP input; repeatable")
    parser.add_argument("--map", action="append", help="Exact archive entry; default is every BSP")
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--game-root", type=Path, help="Read-only installed assets for texture recovery")
    parser.add_argument("--decompile", action="store_true", help="Also produce a recovery report for each map")
    parser.add_argument("--inspect", action="store_true", help="Inspect directories without loading geometry or game assets")
    parser.add_argument("--evidence", action="store_true", help="Analyze normalized geometry/partitions/PVS and retain summary counts only in validation.json")
    parser.add_argument("--obj", action="store_true", help="Also export OBJ geometry and record mesh counts")
    args = parser.parse_args()
    if not args.pak and not args.bsp:
        parser.error("Select at least one --pak or --bsp")
    if args.map and args.bsp:
        parser.error("--map filters archive entries; select loose maps with --bsp")
    if args.inspect and (args.decompile or args.obj):
        parser.error("--inspect and geometry recovery are separate probes")
    if args.evidence and (args.inspect or args.decompile or args.obj):
        parser.error("--evidence is a separate analysis probe")
    if not args.inspect and not args.game:
        parser.error("--game is required for validation/recovery")
    exe, root = args.compiler.resolve(strict=True), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    executable_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
    # Reuse one private input; archive paths never become filesystem destinations.
    staged = root / "input.bsp"
    recovered = root / "recovered.map"
    records, visited = [], set()
    for archive_name, map_name, data in selected_inputs(args):
        visited.add(map_name)
        staged.write_bytes(data)
        asset_root = args.game_root.resolve(strict=True) if args.game_root else root
        base = [str(exe), "-game", args.game, "-fs_basepath", str(asset_root),
                "-fs_homepath", str(root / "home"), "-threads", "2"]
        options = ["-decompile", "-o", str(recovered)] if args.decompile else ["-info"]
        if args.evidence:
            options = ["-bsp-evidence", "-report", str(root / "evidence.json")]
        if args.inspect:
            base = [str(exe)] + (["-game", args.game] if args.game else [])
            options = ["-inspect", "-json"]
        start = time.perf_counter()
        result = subprocess.run([*base, *options, str(staged)], cwd=root, capture_output=True, timeout=180)
        elapsed = time.perf_counter() - start
        log = result.stdout + result.stderr
        (root / f"map-{len(records):03d}.log").write_bytes(log)
        normalized = re.search(rb"Normalized (\d+) non-finite UV pairs", log)
        flare_fogs = re.search(rb"Normalized (\d+) zero-geometry flare fog references", log)
        record = {"archive": archive_name, "map": map_name,
                  "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                  "exit_code": result.returncode, "seconds": elapsed,
                  "normalized_unused_lightmap_uv_pairs": int(normalized[1]) if normalized else 0,
                  "normalized_unused_flare_fogs": int(flare_fogs[1]) if flare_fogs else 0}
        record["missing_texture_warnings"] = log.count(b"Couldn't find image for shader")
        if args.decompile and result.returncode == 0:
            recovery = json.loads(Path(str(recovered) + ".recovery.json").read_text())
            for key in ("brushes", "patches", "faces", "matched_uv_faces", "fallback_uv_faces", "skipped_brushes"):
                record[key] = recovery[key]
            record["native_loss_bytes"] = {item["feature"]: item["bytes"] for item in recovery.get("native_losses", [])}
            record["native_terrain_patches"] = len(recovery.get("native_terrain", []))
            record["native_static_models"] = len(recovery.get("native_static_models", []))
            for key in ("native_terrain_triangles", "native_terrain_removed_triangles", "normalized_unused_native_equations", "inferred_material_faces"):
                record[key] = recovery.get(key, 0)
        if args.inspect:
            inspection = json.loads(result.stdout)
            record["ident"], record["version"] = inspection["ident"], inspection["version"]
            record["valid_layouts"] = [l["id"] for l in inspection["layouts"] if l["valid"]]
        if args.evidence and result.returncode == 0:
            evidence = json.loads((root / "evidence.json").read_text(encoding="utf-8"))
            assert evidence["source"]["sha256"] == record["sha256"]
            for key in ("counts", "visibility", "world_graph"):
                record[key] = evidence[key]
            record["work_units_used"] = evidence["limits"]["work_units_used"]
            record["regions"] = len(evidence["regions"])
            record["report_bytes"] = (root / "evidence.json").stat().st_size
        if args.obj and result.returncode == 0:
            mesh = subprocess.run([*base, "-convert", "-format", "obj", str(staged)], cwd=root, capture_output=True, timeout=180)
            (root / f"mesh-{len(records):03d}.log").write_bytes(mesh.stdout + mesh.stderr)
            record["obj_exit_code"] = mesh.returncode
            if mesh.returncode:
                record["exit_code"] = mesh.returncode
            else:
                digest = hashlib.sha256(); faces = vertices = 0
                with staged.with_suffix(".obj").open("rb") as obj:
                    for line in obj:
                        digest.update(line)
                        vertices += line.startswith(b"v ")
                        faces += line.startswith(b"f ")
                record.update(obj_vertices=vertices, obj_faces=faces, obj_sha256=digest.hexdigest())
        records.append(record)
        assert staged.read_bytes() == data, "Probe modified its input"
        print(args.game or "inspect", map_name, "passed" if record["exit_code"] == 0 else "FAILED", flush=True)
    if args.map and set(args.map) - visited:
        raise ValueError(f"Missing map entries: {sorted(set(args.map) - visited)}")
    assert records, "No BSPs selected"
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == executable_hash, "Compiler changed during probe"
    report = {"schema_version": 1, "timestamp_utc": datetime.now(timezone.utc).isoformat(),
              "kind": "native_bsp_evidence" if args.evidence else "native_directory_inspection" if args.inspect else "native_archive_recovery" if args.decompile else "native_archive_mesh_export" if args.obj else "native_archive_validation",
              "game": args.game, "compiler_sha256": executable_hash,
              "passed": sum(r["exit_code"] == 0 for r in records), "total": len(records), "records": records}
    (root / "validation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{report['passed']}/{len(records)} maps passed; sources unchanged")
    raise SystemExit(0 if report["passed"] == len(records) else 1)


if __name__ == "__main__":
    main()

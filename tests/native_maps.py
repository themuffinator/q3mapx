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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--game", help="Required for native validation/recovery; optional explicit inspection profile")
    parser.add_argument("--pak", type=Path, action="append", required=True)
    parser.add_argument("--map", action="append", help="Exact archive entry; default is every BSP")
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--game-root", type=Path, help="Read-only installed assets for texture recovery")
    parser.add_argument("--decompile", action="store_true", help="Also produce a recovery report for each map")
    parser.add_argument("--inspect", action="store_true", help="Inspect directories without loading geometry or game assets")
    parser.add_argument("--obj", action="store_true", help="Also export OBJ geometry and record mesh counts")
    args = parser.parse_args()
    if args.inspect and (args.decompile or args.obj):
        parser.error("--inspect and geometry recovery are separate probes")
    if not args.inspect and not args.game:
        parser.error("--game is required for validation/recovery")
    exe, root = args.compiler.resolve(strict=True), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    executable_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
    # Reuse one private input; archive paths never become filesystem destinations.
    staged = root / "input.bsp"
    recovered = root / "recovered.map"
    records, visited = [], set()
    for archive_path in args.pak:
        archive_path = archive_path.resolve(strict=True)
        stat = archive_path.stat()
        with zipfile.ZipFile(archive_path) as archive:
            for info in archive.infolist():
                if not info.filename.lower().endswith(".bsp") or (args.map and info.filename not in args.map):
                    continue
                visited.add(info.filename)
                if info.file_size > 1024 * 1024 * 1024:
                    raise ValueError(f"Map exceeds the 1 GiB optional probe limit: {info.filename}")
                data = archive.read(info)
                staged.write_bytes(data)
                asset_root = args.game_root.resolve(strict=True) if args.game_root else root
                base = [str(exe), "-game", args.game, "-fs_basepath", str(asset_root),
                        "-fs_homepath", str(root / "home"), "-threads", "2"]
                options = ["-decompile", "-o", str(recovered)] if args.decompile else ["-info"]
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
                record = {"archive": archive_path.name, "map": info.filename,
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
                    for key in ("native_terrain_triangles", "native_terrain_removed_triangles", "normalized_unused_native_equations"):
                        record[key] = recovery.get(key, 0)
                if args.inspect:
                    inspection = json.loads(result.stdout)
                    record["ident"], record["version"] = inspection["ident"], inspection["version"]
                    record["valid_layouts"] = [l["id"] for l in inspection["layouts"] if l["valid"]]
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
                print(args.game or "inspect", info.filename, "passed" if record["exit_code"] == 0 else "FAILED", flush=True)
        after = archive_path.stat()
        assert (stat.st_size, stat.st_mtime_ns) == (after.st_size, after.st_mtime_ns), "Source archive changed"
    if args.map and set(args.map) - visited:
        raise ValueError(f"Missing map entries: {sorted(set(args.map) - visited)}")
    assert records, "No BSPs selected"
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == executable_hash, "Compiler changed during probe"
    report = {"schema_version": 1, "timestamp_utc": datetime.now(timezone.utc).isoformat(),
              "kind": "native_directory_inspection" if args.inspect else "native_archive_recovery" if args.decompile else "native_archive_mesh_export" if args.obj else "native_archive_validation",
              "game": args.game, "compiler_sha256": executable_hash,
              "passed": sum(r["exit_code"] == 0 for r in records), "total": len(records), "records": records}
    (root / "validation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{report['passed']}/{len(records)} maps passed; source archives unchanged")
    raise SystemExit(0 if report["passed"] == len(records) else 1)


if __name__ == "__main__":
    main()

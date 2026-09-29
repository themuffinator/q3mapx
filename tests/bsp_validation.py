"""Corrupt-file and cross-format CLI tests. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
from pathlib import Path
import struct
import subprocess

from fixtures import create_fixture
from integration import Bsp, run


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    exe = args.compiler.resolve(strict=True)
    directory = args.work_dir.resolve()
    source = create_fixture(directory)
    base = ["-game", "quake3", "-fs_basepath", str(directory), "-threads", "2"]
    run(exe, [*base, "-meta", source], directory, "prepare")
    valid = Bsp(source.with_suffix(".bsp"))
    cases = {"truncated-header": b"IBSP", "truncated-data": valid.data[:-40]}

    def integer(name, offset, value):
        data = bytearray(valid.data)
        struct.pack_into("<i", data, offset, value)
        cases[name] = data

    integer("negative-lump-offset", 8 + 2 * 8, -4)
    integer("negative-lump-length", 8 + 2 * 8 + 4, -16)
    integer("overflow-lump-offset", 8 + 2 * 8, 2147483640)
    integer("header-overlap", 8 + 2 * 8, 0)
    integer("bad-brush-side-range", valid.lumps[8][0], 2147483647)
    integer("negative-brush-count", valid.lumps[8][0] + 4, -1)
    integer("bad-model-range", valid.lumps[7][0] + 24, 2147483647)
    integer("bad-side-plane", valid.lumps[9][0], 2147483647)
    integer("bad-side-shader", valid.lumps[9][0] + 4, -1)
    integer("bad-surface-shader", valid.lumps[13][0], 2147483647)
    integer("bad-surface-type", valid.lumps[13][0] + 8, 2147483647)
    # NRC deduplicates index sequences, leaving a spare prefix in this fixture.
    first_used_index = struct.unpack_from("<i", valid.data, valid.lumps[13][0] + 104 + 20)[0]
    integer("bad-triangle-index", valid.lumps[11][0] + first_used_index * 4, 2147483647)
    integer("bad-node-plane", valid.lumps[3][0], -1)
    integer("node-cycle", valid.lumps[3][0] + 4, 0)
    integer("bad-node-leaf", valid.lumps[3][0] + 4, -2147483648)
    integer("bad-leaf-range", valid.lumps[4][0] + 32, 2147483647)
    integer("nan-vertex", valid.lumps[10][0], 0x7FC00000)
    integer("nan-plane", valid.lumps[2][0], 0x7FC00000)
    integer("short-vis", 8 + 16 * 8 + 4, 4)
    # Point the visibility lump at four valid payload bytes, so its own size check is exercised.
    struct.pack_into("<i", cases["short-vis"], 8 + 16 * 8, valid.lumps[2][0])
    data = bytearray(valid.data)
    data[valid.lumps[1][0]:valid.lumps[1][0] + 64] = b"x" * 64
    cases["unterminated-shader"] = data
    data = bytearray(valid.data)
    entity_offset, entity_size = valid.lumps[0]
    model_offset = data.find(b'"model" "*1"', entity_offset, entity_offset + entity_size)
    assert model_offset >= 0
    data[model_offset:model_offset + 12] = data[model_offset:model_offset + 12].replace(b"*1", b"*9")
    cases["invalid-entity-model"] = data
    for i in range(valid.summary()["surfaces"]):
        offset = valid.lumps[13][0] + i * 104
        if struct.unpack_from("<i", valid.data, offset + 8)[0] == 2:
            integer("invalid-patch-size", offset + 96, 2147483647)
            break
    assert "invalid-patch-size" in cases

    for name, data in cases.items():
        path = directory / f"{name}.bsp"
        path.write_bytes(data)
        for forced in (False, True):
            command = [str(exe), *base, *( ["-force"] if forced else [] ),
                       "-convert", "-format", "map_220", str(path)]
            result = subprocess.run(command, cwd=directory, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, timeout=15)
            output = result.stdout.decode(errors="replace")
            (directory / f"{name}-{'force' if forced else 'strict'}.log").write_text(output, encoding="utf-8")
            assert result.returncode == 1, f"{name}: expected controlled failure, got {result.returncode}\n{output}"
            assert "Invalid BSP:" in output, f"{name}: wrong failure\n{output}"

    # Lumps need not be aligned. Shift all payload bytes by one and adjust the offsets.
    data = bytearray(valid.data[:152] + b"!" + valid.data[152:])
    for i, (offset, size) in enumerate(valid.lumps):
        if offset >= 152:
            struct.pack_into("<i", data, 8 + i * 8, offset + 1)
    unaligned = directory / "unaligned.bsp"
    unaligned.write_bytes(data)
    run(exe, [*base, "-convert", "-format", "map_220", unaligned], directory, "unaligned")
    # Classic Q3 has 17 header lumps; NRC's writer leaves an extra eight-byte slot.
    data = bytearray(valid.data[:144] + valid.data[152:])
    for i, (offset, size) in enumerate(valid.lumps):
        if offset >= 152:
            struct.pack_into("<i", data, 8 + i * 8, offset - 8)
    compact = directory / "classic-header.bsp"
    compact.write_bytes(data)
    run(exe, [*base, "-convert", "-format", "map_220", compact], directory, "classic-header")
    run(exe, [*base, "-repack", "-analyze", source.with_suffix(".bsp")], directory, "partial-load")

    # Check the two additional supported header families with original generated assets.
    for game in ("quakelive", "ja"):
        game_root = directory / game
        game_map = create_fixture(game_root)
        options = ["-game", game, "-fs_basepath", game_root, "-fs_basegame", "baseq3", "-threads", 2]
        run(exe, [*options, "-meta", game_map], game_root, "compile")
        run(exe, [*options, "-light", "-fast", game_map], game_root, "light")
        run(exe, [*options, "-convert", "-format", "map_220", game_map.with_suffix(".bsp")], game_root, "decompile")
        if game == "ja":
            data = bytearray(game_map.with_suffix(".bsp").read_bytes())
            offset, length = struct.unpack_from("<ii", data, 8 + 17 * 8)
            assert length > 0, "RBSP fixture must exercise lightgrid indirection"
            struct.pack_into("<H", data, offset, 65535)
            corrupted = game_root / "bad-grid.bsp"
            corrupted.write_bytes(data)
            result = subprocess.run([str(exe), *map(str, options), "-force", "-convert", "-format", "map_220", str(corrupted)],
                                    cwd=game_root, capture_output=True, timeout=15)
            assert result.returncode == 1 and b"lightgrid array index" in result.stdout, result.stdout
    print(f"{len(cases) * 2} malformed-file checks, unaligned lumps, IBSP47 and RBSP passed")


if __name__ == "__main__":
    main()

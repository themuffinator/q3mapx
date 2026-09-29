"""Original asset-independent map fixtures. SPDX-License-Identifier: GPL-3.0-or-later."""
from pathlib import Path
import struct


def box(lo, hi, texture="q3mapx/stone", transform="13 -7 23 0.5 0.75"):
    x0, y0, z0 = lo
    x1, y1, z1 = hi
    faces = [
        ((x0, y0, z0), (x0, y1, z0), (x0, y0, z1)),
        ((x1, y0, z0), (x1, y0, z1), (x1, y1, z0)),
        ((x0, y0, z0), (x0, y0, z1), (x1, y0, z0)),
        ((x0, y1, z0), (x1, y1, z0), (x0, y1, z1)),
        ((x0, y0, z0), (x1, y0, z0), (x0, y1, z0)),
        ((x0, y0, z1), (x0, y1, z1), (x1, y0, z1)),
    ]
    return "{\n" + "\n".join(
        " ".join("( %g %g %g )" % p for p in face) + f" {texture} {transform} 0 0 0"
        for face in faces
    ) + "\n}\n"


def create_fixture(root: Path, *, dense=False, patch=True, grid=11) -> Path:
    """Create only generated test content beneath the explicitly supplied directory."""
    root = root.resolve()
    game = root / "baseq3"
    for directory in (game / "maps", game / "scripts", game / "textures/q3mapx"):
        directory.mkdir(parents=True, exist_ok=True)
    (game / "scripts/shaderlist.txt").write_text("q3mapx_tests\n", encoding="utf-8")
    (game / "scripts/q3mapx_tests.shader").write_text("""
textures/q3mapx/stone
{
    qer_editorimage textures/q3mapx/checker.tga
    {
        map textures/q3mapx/checker.tga
    }
}
textures/common/origin
{
    surfaceparm origin
    surfaceparm nodraw
}
""", encoding="utf-8")
    pixels = bytearray()
    for y in range(64):
        for x in range(64):
            pixels.extend((80, 140, 220) if (x // 8 + y // 8) % 2 else (180, 80, 40))
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 64, 64, 24, 0x20)
    (game / "textures/q3mapx/checker.tga").write_bytes(header + pixels)
    extent = (grid // 2 + 1) * 128 if dense else 256
    walls = [
        ((-extent-16, -extent-16, -16), (extent+16, extent+16, 0)),
        ((-extent-16, -extent-16, 256), (extent+16, extent+16, 272)),
        ((-extent-16, -extent-16, 0), (-extent, extent+16, 256)),
        ((extent, -extent-16, 0), (extent+16, extent+16, 256)),
        ((-extent, -extent-16, 0), (extent, -extent, 256)),
        ((-extent, extent, 0), (extent, extent+16, 256)),
    ]
    geometry = "".join(box(lo, hi) for lo, hi in walls)
    if dense:
        limit = grid // 2 * 128
        for y in range(-limit, limit + 1, 128):
            for x in range(-limit, limit + 1, 128):
                geometry += box((x-20, y-20, 0), (x+20, y+20, 80 + (x+y) % 112))
    else:
        geometry += box((-48, -48, 0), (48, 48, 96))
    if patch:
        geometry += """{
patchDef2
{
q3mapx/stone
( 3 3 0 0 0 )
(
( ( -192 -192 16 0 0 ) ( -192 -128 64 0 1 ) ( -192 -64 16 0 2 ) )
( ( -128 -192 16 1 0 ) ( -128 -128 112 1 1 ) ( -128 -64 16 1 2 ) )
( ( -64 -192 16 2 0 ) ( -64 -128 64 2 1 ) ( -64 -64 16 2 2 ) )
)
}
}
"""
    text = '{\n"classname" "worldspawn"\n"message" "q3mapx regression"\n' + geometry + "}\n"
    text += '{\n"classname" "info_player_deathmatch"\n"origin" "160 160 32"\n}\n'
    text += '{\n"classname" "light"\n"origin" "0 0 224"\n"light" "450"\n}\n'
    text += '{\n"classname" "func_door"\n"targetname" "test_door"\n"angle" "90"\n'
    text += box((96, -48, 16), (128, 48, 112))
    text += box((104, -8, 56), (120, 8, 72), "common/origin", "0 0 0 1 1") + "}\n"
    path = game / "maps/fixture.map"
    path.write_text(text, encoding="utf-8")
    return path


def create_lighting_fixture(root: Path, *, dense=False, grid=11) -> Path:
    """Exercise texture-filtered shadows, sky, emitters, patches and model origins."""
    source = create_fixture(root, dense=dense, grid=grid)
    game = source.parent.parent
    scripts = game / "scripts/q3mapx_tests.shader"
    scripts.write_text(scripts.read_text(encoding="utf-8") + """
textures/q3mapx/fence
{
    q3map_lightimage textures/q3mapx/fence.tga
    surfaceparm trans
    surfaceparm nonsolid
    surfaceparm alphashadow
    cull none
    { map textures/q3mapx/fence.tga alphaFunc GE128 }
}
textures/q3mapx/filter
{
    q3map_lightimage textures/q3mapx/filter.tga
    surfaceparm trans
    surfaceparm nonsolid
    surfaceparm lightfilter
    cull none
    { map textures/q3mapx/filter.tga blendFunc filter }
}
textures/q3mapx/sky
{
    q3map_lightimage textures/q3mapx/checker.tga
    surfaceparm sky
    surfaceparm noimpact
    surfaceparm nolightmap
    q3map_sun 1 0.85 0.7 90 35 60
    skyparms - 512 -
}
textures/q3mapx/emitter
{
    q3map_lightimage textures/q3mapx/checker.tga
    q3map_surfacelight 600
    { map textures/q3mapx/checker.tga }
}
""", encoding="utf-8")
    header = struct.pack("<BBBHHBHHHHBB", 0, 0, 2, 0, 0, 0, 0, 0, 64, 64, 32, 0x28)
    fence, filtered = bytearray(), bytearray()
    for y in range(64):
        for x in range(64):
            fence.extend((220, 220, 220, 255 if (x // 8 + y // 8) % 2 else 0))
            filtered.extend((45, 220, 65, 128))  # TGA BGRA: green filter
    (game / "textures/q3mapx/fence.tga").write_bytes(header + fence)
    (game / "textures/q3mapx/filter.tga").write_bytes(header + filtered)
    text = source.read_text(encoding="utf-8")
    extent = (grid // 2 + 1) * 128 if dense else 256
    ceiling = ((-extent-16, -extent-16, 256), (extent+16, extent+16, 272))
    text = text.replace(box(*ceiling), box(*ceiling, "q3mapx/sky"))
    panels = box((-224, 32, 144), (-64, 224, 148), "q3mapx/fence", "0 0 0 1 1")
    panels += box((64, 32, 144), (224, 224, 148), "q3mapx/filter", "0 0 0 1 1")
    panels += box((-32, -240, 96), (32, -232, 160), "q3mapx/emitter")
    marker = '\n}\n{\n"classname" "info_player_deathmatch"'
    assert text.count(marker) == 1
    text = text.replace(marker, "\n" + panels + marker)
    source.write_text(text, encoding="utf-8")
    return source

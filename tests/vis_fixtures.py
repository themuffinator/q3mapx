"""Matched first-party VIS workloads. SPDX-License-Identifier: GPL-3.0-or-later."""
import math
import re
from fixtures import box, create_fixture


def create_vis_fixture(root, *, grid=5, detail=False):
    source = create_fixture(root, dense=True, patch=False, grid=grid)
    if detail:
        text = source.read_text()
        limit = grid // 2 * 128
        for y in range(-limit, limit+1, 128):
            for x in range(-limit, limit+1, 128):
                brush = box((x-20, y-20, 0), (x+20, y+20, 80+(x+y) % 112))
                assert text.count(brush) == 1
                text = text.replace(brush, brush.replace(' 0 0 0\n', ' 134217728 0 0\n'))
        source.write_text(text)
    return source


def create_round_vis_fixture(root):
    """A sealed 64-sided corridor, split by two structural hint brushes."""
    source=create_fixture(root,patch=False)
    shader=source.parent.parent/'scripts/q3mapx_tests.shader'
    shader.write_text(shader.read_text()+'''
textures/q3mapx/hint
{
    qer_editorimage textures/q3mapx/checker.tga
    surfaceparm hint
    surfaceparm structural
    surfaceparm nodraw
    surfaceparm nonsolid
    surfaceparm trans
}
''')
    geometry=box((-112,-384,-384),(-96,384,384))+box((224,-384,-384),(240,384,384))
    for i in range(64):
        theta=2*math.pi*i/64
        def rotate(match):
            x,y,z=map(float,match[1].split())
            return '( %.9g %.9g %.9g )'%(x,y*math.cos(theta)-z*math.sin(theta),y*math.sin(theta)+z*math.cos(theta))
        geometry+=re.sub(r'\( ([^)]+) \)',rotate,box((-112,128,-24),(240,192,24)))
    for x in (0,64): geometry+=box((x,-256,-256),(x+1,256,256),'q3mapx/hint')
    # Stay within one automatic BSP block so its boundaries do not quarter the
    # circular openings into small polygons and bypass the large-winding path.
    geometry=re.sub(r'\( ([^)]+) \)',lambda m:'( %.9g %.9g %.9g )'%tuple(float(x)+512 for x in m[1].split()),geometry)
    source.write_text('{\n"classname" "worldspawn"\n'+geometry+'}\n'
        '{\n"classname" "info_player_deathmatch"\n"origin" "432 512 512"\n}\n')
    return source

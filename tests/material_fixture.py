"""Original painted-patch material corpus shared by native and engine rendering.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import json
from pathlib import Path
import struct
from fixtures import box
from patch_paint import painted


MATERIALS = {
    'vertex': '{ map $whiteimage rgbGen vertex alphaGen vertex }',
    'identity': '{ map $whiteimage rgbGen identity alphaGen identity }',
    'alpha': 'surfaceparm trans { map $whiteimage rgbGen vertex alphaGen vertex blendFunc blend }',
    'inverse-alpha': 'surfaceparm trans { map $whiteimage rgbGen vertex alphaGen oneMinusVertex blendFunc blend }',
    'cutout': '{ map $whiteimage rgbGen vertex alphaGen vertex alphaFunc GE128 }',
    'cutout-inverse': '{ map $whiteimage rgbGen vertex alphaGen vertex alphaFunc LT128 }',
    'texture': '{ map textures/q3mapx/material-grid.tga rgbGen vertex alphaGen vertex }',
    'clamp': '{ clampMap textures/q3mapx/material-grid.tga rgbGen vertex alphaGen vertex }',
    'texture-alpha': 'surfaceparm trans { map textures/q3mapx/material-grid.tga rgbGen vertex alphaGen vertex blendFunc blend }',
    'zero-alpha': 'surfaceparm trans { map textures/q3mapx/material-transparent.tga rgbGen vertex alphaGen vertex blendFunc blend }',
    'texture-cutout': '{ map textures/q3mapx/material-grid.tga rgbGen vertex alphaGen vertex alphaFunc GE128 }',
    'lightmap': '{ map $whiteimage rgbGen vertex alphaGen vertex } { map $lightmap blendFunc filter depthFunc equal }',
    'two-stage': '{ map textures/q3mapx/material-grid.tga rgbGen identity } { map $whiteimage rgbGen vertex alphaGen vertex blendFunc blend depthFunc equal }',
    'add': 'surfaceparm trans { map $whiteimage rgbGen vertex blendFunc add }',
    'filter': 'surfaceparm trans { map $whiteimage rgbGen vertex blendFunc filter }',
    'back': 'cull back { map $whiteimage rgbGen vertex }',
    'two-sided': 'cull none { map $whiteimage rgbGen vertex }',
}
CAMERAS = [('top',[0,0,256,90,90,90]), ('oblique',[160,-160,256,40,135,90])]


def patch_text(curved=False):
    # Strong alpha boundary, non-affine RGB, nontrivial texture coordinates.
    text=painted(subdivisions=16,curved=curved,origin=(-96,-96,64),extent=192,shader='material-base')
    import re
    def replace(match):
        values=match.group(1).split()
        if len(values)!=9: return match.group(0)
        u,v=int(float(values[3])),int(float(values[4]))
        values[3:5]=[str(u*0.75-0.25),str(v*0.75-0.25)]
        values[8]=str((0,128,255)[u])
        return '( '+' '.join(values)+' )'
    return re.sub(r'\( ([^()]*) \)',replace,text)


def assets(game: Path):
    shader=game/'scripts/q3mapx_tests.shader'
    additions='\ntextures/q3mapx/material-base\n{\n{ map $whiteimage rgbGen vertex alphaGen vertex }\n{ map $lightmap blendFunc filter }\n}\n'
    additions+='textures/q3mapx/material-background\n{\n{ map textures/q3mapx/material-background.tga rgbGen identity }\n}\n'
    for name,body in MATERIALS.items():
        additions+=f'textures/q3mapx/material-{name}\n{{\nqer_editorimage textures/q3mapx/checker.tga\n{body}\n}}\n'
    additions+='textures/q3mapx/material-unsupported\n{\n{ map $whiteimage tcMod scroll 1 1 }\n}\n'
    additions+='textures/q3mapx/material-missing\n{\n{ map textures/q3mapx/no-such-image.tga }\n}\n'
    shader.write_text(shader.read_text(encoding='utf-8')+additions,encoding='utf-8')
    header=struct.pack('<BBBHHBHHHHBB',0,0,2,0,0,0,0,0,8,8,32,0x28)
    rgba=[]
    for y in range(8):
        for x in range(8):
            rgba.extend((32+x*24,48+y*24,216-x*16,32 if (x//2+y//2)%2 else 224)) # BGRA
    (game/'textures/q3mapx/material-grid.tga').write_bytes(header+bytes(rgba))
    (game/'textures/q3mapx/material-transparent.tga').write_bytes(header+bytes((32,128,224,0))*64)
    (game/'textures/q3mapx/material-background.tga').write_bytes(header+bytes((128,96,64,255))*64)


def native_inputs(root: Path):
    for shape in ('flat','curved'):
        (root/f'material-{shape}.txt').write_text(patch_text(shape=='curved'),encoding='utf-8')
    (root/'material-cases.json').write_text(json.dumps({'materials':list(MATERIALS),'cameras':CAMERAS}),encoding='utf-8')


def scene(curved=False):
    walls=[((-528,-528,-16),(528,528,0)),((-528,-528,512),(528,528,528)),
           ((-528,-528,0),(-512,528,512)),((512,-528,0),(528,528,512)),
           ((-512,-528,0),(512,-512,512)),((-512,512,0),(512,528,512))]
    # No light entities are needed for neutral rendering. Exercise the normal
    # LIGHT source-loading path, including unrelated unfinished shader entries.
    return ('{\n"classname" "worldspawn"\n'+''.join(box(lo,hi,'q3mapx/material-background') for lo,hi in walls)+
            patch_text(curved)+'}\n{\n"classname" "info_player_deathmatch"\n"origin" "160 160 32"\n}\n')

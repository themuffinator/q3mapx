"""Compare BSPs with/without retained source trailers in Quake3e's render target.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from integration import run
from material_fixture import scene
from patch_source import archive, FOOTER
from paint_material_render import image


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('compiler','engine','engine-source','assets','materials','work-dir'):
        p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--fit',action='store_true',help='Compare original triangle-only BSP with an archive-free fitted/rebuilt BSP')
    a=p.parse_args()
    assert sys.platform.startswith('linux')
    root=a.work_dir.resolve(); root.mkdir(parents=True,exist_ok=True)
    library=root/'cgamex86_64.so'
    subprocess.run(['gcc','-shared','-fPIC','-std=c99','-O2','-I'+str(a.engine_source.resolve()/'code'),
        str(Path(__file__).with_name('geometry_cgame.c')),'-o',str(library),'-lm'],cwd=root,check=True)
    cases=[]
    for shape in ('flat','curved'):
        directory=root/shape; game=directory/'baseq3'
        for sub in ('scripts','textures'): shutil.copytree(a.materials/sub,game/sub,dirs_exist_ok=True)
        if a.fit:
            shader=game/'scripts/q3mapx_tests.shader'
            text=shader.read_text(encoding='utf-8')
            assert text.count('textures/q3mapx/material-base\n{\n')==1
            shader.write_text(text.replace('textures/q3mapx/material-base\n{\n','textures/q3mapx/material-base\n{\nsurfaceparm nonsolid\n'),encoding='utf-8')
        (game/'maps').mkdir(parents=True,exist_ok=True)
        shutil.copyfile(library,game/library.name)
        source=game/'maps/fixture.map'; source.write_text(scene(shape=='curved'),encoding='utf-8')
        base=['-game','quake3','-fs_basepath',directory,'-fs_homepath',directory/'home','-threads',1]
        run(a.compiler.resolve(),[*base,source],root,shape+'-bsp')
        run(a.compiler.resolve(),[*base,'-light','-fast',source],root,shape+'-light')
        data=source.with_suffix('.bsp').read_bytes(); payload=archive(data)
        stripped=data[:-FOOTER-len(payload)]
        variants=(('archive',data),('stripped',stripped))
        if a.fit:
            legacy=source.with_name('legacy.bsp'); legacy.write_bytes(stripped)
            recovered=source.with_name('recovered.map')
            run(a.compiler.resolve(),[*base,'-decompile','-patch-recovery','fit','-patch-colors','rgba','-o',recovered,legacy],root,shape+'-fit')
            report=json.loads(Path(str(recovered)+'.recovery.json').read_text(encoding='utf-8'))
            assert report['patch_recovery']['triangle_fitting']['fitted_patches']==1
            run(a.compiler.resolve(),[*base,recovered],root,shape+'-rebuild')
            run(a.compiler.resolve(),[*base,'-light','-fast',recovered],root,shape+'-relight')
            variants=(('original',stripped),('rebuilt',recovered.with_suffix('.bsp').read_bytes()))
        options={'fs_basepath':str(a.assets.resolve().parent),'fs_basegame':a.assets.name,'fs_homepath':str(directory),
            'fs_game':'baseq3','r_fullscreen':'0','r_mode':'-1','r_customwidth':'640','r_customheight':'480',
            'in_mouse':'0','in_joystick':'0','in_nograb':'1','s_initsound':'0','vm_cgame':'0','vm_game':'2','vm_ui':'2',
            'sv_pure':'0','bot_enable':'0','r_vbo':'0','r_vertexLight':'0','r_fullbright':'1','r_overBrightBits':'0',
            'r_mapOverBrightBits':'0','r_gamma':'1','r_intensity':'1','r_picmip':'0','net_enabled':'0'}
        for label,bsp in variants:
            source.with_suffix('.bsp').write_bytes(bsp)
            (game/'capture.cfg').write_text('set com_fixedtime 16\nset con_notifytime 0\nset r_finish 1\n'
                'wait 45\nset q3mapx_camera "0 0 256 90 90 90"\nwait 5\nscreenshot '+label+'\nwait 3\nquit\n',encoding='utf-8')
            command=[str(a.engine.resolve()),'+safe']
            for key,value in options.items(): command+=['+set',key,value]
            command+=['+devmap','fixture','+exec','capture.cfg']
            env=dict(os.environ,SDL_VIDEODRIVER='offscreen',SDL_AUDIODRIVER='dummy',LIBGL_ALWAYS_SOFTWARE='1',TMPDIR=str(root))
            result=subprocess.run(command,cwd=root,env=env,capture_output=True,timeout=60)
            log=result.stdout+result.stderr; (directory/(label+'.log')).write_bytes(log)
            assert result.returncode==0 and b'SDL using driver "offscreen"' in log and b'q3mapx deterministic renderer fixture active' in log,log[-3000:]
        pixels=image(game/f'screenshots/{variants[0][0]}.tga')
        assert pixels==image(game/f'screenshots/{variants[1][0]}.tga') and len(set(pixels))>32
        cases.append(dict(shape=shape,pixels_equal=True,pixel_sha256=hashlib.sha256(pixels).hexdigest()))
    (root/'validation.json').write_text(json.dumps(dict(cases=cases,mode='triangle_reconstruction' if a.fit else 'source_archive',
        compiler_sha256=hashlib.sha256(a.compiler.resolve().read_bytes()).hexdigest(),engine_sha256=hashlib.sha256(a.engine.resolve().read_bytes()).hexdigest(),
        capture='Engine screenshot command, SDL offscreen, input/network disabled'),indent=2)+'\n',encoding='utf-8')
    print('Original/rebuilt BSP engine renders match for both patch shapes' if a.fit else 'Archive/stripped BSP engine renders match for both patch shapes')


if __name__=='__main__': main()

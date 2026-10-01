"""Compare NRC's owned material framebuffer with compiled BSPs in Quake3e.
Uses SDL offscreen and the engine screenshot command; no OS capture/input.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from integration import Bsp, run
from surface_density import extras
from material_fixture import MATERIALS, CAMERAS, scene


def image(path):
    data=path.read_bytes()
    if path.suffix=='.ppm':
        header=data.split(b'\n',3)
        assert header[:3]==[b'P6',b'640 480',b'255']
        pixels=header[3]
    else:
        assert data[1:3]==b'\0\2' and data[16]==24
        assert struct.unpack_from('<HH',data,12)==(640,480)
        pixels=data[18+data[0]:]
        if not data[17]&32: pixels=b''.join(pixels[y*1920:(y+1)*1920] for y in reversed(range(480)))
        rgb=bytearray(len(pixels)); rgb[0::3]=pixels[2::3]; rgb[1::3]=pixels[1::3]; rgb[2::3]=pixels[0::3]; pixels=rgb
    assert len(pixels)==640*480*3
    return pixels


def compare(a,b):
    errors=[abs(x-y) for x,y in zip(image(a),image(b))]
    return {'max_channel_error':max(errors),'mean_channel_error':sum(errors)/len(errors),
            'pixels_over_two':sum(max(errors[p:p+3])>2 for p in range(0,len(errors),3))}


def mesh_check(source,native):
    bsp=Bsp(source.with_suffix('.bsp')); srf=extras(source)
    authored=json.loads(native.read_text(encoding='utf-8'))
    def canonical(points):
        points=[tuple(round(float(v),6) for v in p) for p in points]
        return min(tuple(points[i:]+points[:i]) for i in range(3))
    want=sorted(canonical([authored['vertices'][i] for i in authored['indices'][p:p+3]])
                for p in range(0,len(authored['indices']),3))
    actual=[]
    for index,p in enumerate(range(0,len(bsp.lump(13)),104)):
        shader,fog,kind,first,count,first_index,index_count=struct.unpack_from('<7i',bsp.lump(13),p)
        if not srf[index].get('patchPaintMode') or kind==2: continue
        vertices=[]
        for v in range(first,first+count):
            xyzuv=struct.unpack_from('<5f',bsp.lump(10),v*44)
            vertices.append((*xyzuv,*bsp.lump(10)[v*44+40:v*44+44]))
        indices=struct.unpack_from('<'+str(index_count)+'i',bsp.lump(11),first_index*4)
        actual.extend(canonical([vertices[i] for i in indices[p:p+3]]) for p in range(0,index_count,3))
    assert sorted(actual)==want,'Native and compiled geometry/UV/RGBA triangles differ'
    return len(actual)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('engine','engine-source','assets','compiler','native-dir','work-dir'): p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    assert sys.platform.startswith('linux'),'Requires Linux SDL offscreen'
    project=Path(__file__).resolve().parents[2]
    root=a.work_dir.resolve(); native=a.native_dir.resolve(); compiler=a.compiler.resolve()
    assert root.is_relative_to(project/'build') or root.is_relative_to(project/'.agents/tmp')
    root.mkdir(parents=True,exist_ok=True)
    engine=a.engine.resolve(); source=a.engine_source.resolve(); assets=a.assets.resolve()
    library=root/'cgamex86_64.so'
    subprocess.run(['gcc','-shared','-fPIC','-std=c99','-O2','-I'+str(source/'code'),
        str(Path(__file__).with_name('geometry_cgame.c')),'-o',str(library),'-lm'],cwd=root,check=True)
    report={'capture':'Native owned framebuffer / Quake3e screenshot, SDL offscreen; no OS capture or input',
            'contract':'neutral lighting, map/engine overbright 0, gamma/intensity 1, bilinear base mip',
            'tolerance':{'max_pixels_over_two':307,'max_mean_channel_error':0.10},
            'engine_sha256':hashlib.sha256(engine.read_bytes()).hexdigest(),'cases':[],'failures':[]}
    work=[]
    for shape in ('flat','curved'):
        directory=root/shape; game=directory/'baseq3'
        for subdir in ('scripts','textures'): shutil.copytree(native/'engine/baseq3'/subdir,game/subdir,dirs_exist_ok=True)
        (game/'maps').mkdir(parents=True,exist_ok=True)
        mapfile=game/'maps/fixture.map'; mapfile.write_text(scene(shape=='curved'),encoding='utf-8')
        base=['-game','quake3','-fs_basepath',directory,'-fs_homepath',directory/'home','-threads','1']
        run(compiler,[*base,mapfile],directory,'bsp')
        triangles=mesh_check(mapfile,native/f'material-{shape}-mesh.json')
        run(compiler,[*base,'-light','-fast',mapfile],directory,'light')
        assert mesh_check(mapfile,native/f'material-{shape}-mesh.json')==triangles
        bsp=Bsp(mapfile.with_suffix('.bsp'))
        for material in MATERIALS:
            # Assign the final shader before the engine loads/sorts the BSP.
            # Runtime shader remapping retains the old draw ordering and cannot
            # validate a change between opaque and translucent material classes.
            payload=bytearray(mapfile.with_suffix('.bsp').read_bytes()); replaced=0
            for offset in range(0,len(bsp.lump(1)),72):
                if bsp.lump(1)[offset:offset+64].split(b'\0')[0]!=b'textures/q3mapx/material-base': continue
                start=bsp.lumps[1][0]+offset
                payload[start:start+64]=f'textures/q3mapx/material-{material}'.encode().ljust(64,b'\0'); replaced+=1
            assert replaced
            work.append((shape,material,triangles,payload))

    def render_case(item):
        shape,material,triangles,payload=item
        directory=root/shape/material; game=directory/'baseq3'
        for subdir in ('scripts','textures'): shutil.copytree(native/'engine/baseq3'/subdir,game/subdir,dirs_exist_ok=True)
        (game/'maps').mkdir(parents=True,exist_ok=True)
        (game/'maps/fixture.bsp').write_bytes(payload)
        shutil.copyfile(library,game/library.name)
        commands=['set com_fixedtime 16','set com_maxfps 125','set com_maxfpsUnfocused 125',
                  'set con_notifytime 0','set r_finish 1','wait 45']; labels=[]
        for camera,coords in CAMERAS:
            label=f'{material}-{camera}'; labels.append(label)
            commands += ['set q3mapx_camera "'+' '.join(map(str,coords))+'"','wait 5',f'screenshot {label}','wait 3']
        commands+=['set q3mapx_camera "0 0 256 90 90 90"',
                   'wait 5','screenshot repeat','wait 3','quit']
        (game/'q3mapx_capture.cfg').write_text('\n'.join(commands)+'\n',encoding='utf-8')
        options={'fs_basepath':str(assets.parent),'fs_basegame':assets.name,'fs_homepath':str(directory),'fs_game':'baseq3',
            'r_fullscreen':'0','r_mode':'-1','r_customwidth':'640','r_customheight':'480','in_mouse':'0','in_joystick':'0',
            'in_nograb':'1','s_initsound':'0','vm_cgame':'0','vm_game':'2','vm_ui':'2','sv_pure':'0','bot_enable':'0',
            'r_vbo':'0','r_ext_texture_filter_anisotropic':'0','r_vertexLight':'0','r_fullbright':'1','r_overBrightBits':'0','r_mapOverBrightBits':'0',
            'r_gamma':'1','r_intensity':'1','r_picmip':'0','r_textureMode':'GL_LINEAR','net_enabled':'0'}
        # The reference engine accepts only 32 '+' command lines. Non-latched
        # timing/console settings belong in the capture script above.
        assert len(options)+3<=31
        command=[str(engine),'+safe']
        for key,value in options.items(): command+=['+set',key,value]
        command+=['+devmap','fixture','+exec','q3mapx_capture.cfg']
        env=dict(os.environ,SDL_VIDEODRIVER='offscreen',SDL_AUDIODRIVER='dummy',LIBGL_ALWAYS_SOFTWARE='1',TMPDIR=str(root))
        (directory/'command.json').write_text(json.dumps(command,indent=2)+'\n',encoding='utf-8')
        with (directory/'engine.log').open('wb') as log_file:
            result=subprocess.run(command,cwd=root,env=env,stdout=log_file,stderr=subprocess.STDOUT,timeout=60)
        log=(directory/'engine.log').read_text(encoding='utf-8',errors='replace')
        assert result.returncode==0 and 'q3mapx deterministic renderer fixture active' in log,log[-3000:]
        assert 'SDL using driver "offscreen"' in log and 'GL_RENDERER: llvmpipe' in log
        assert image(game/f'screenshots/{material}-top.tga')==image(game/'screenshots/repeat.tga'),'Engine self repeat must be byte-identical'
        results=[]
        for label in labels:
            assert f'Wrote screenshots/{label}.tga' in log
            metrics=compare(native/f'material-{shape}-{label}.ppm',game/f'screenshots/{label}.tga')
            results.append({'shape':shape,'case':label,'triangles':triangles,**metrics})
        print(shape,material,len(labels),'render comparisons',flush=True)
        return results
    with ThreadPoolExecutor(max_workers=2) as pool:
        for results in pool.map(render_case,work): report['cases'].extend(results)
    for result in report['cases']:
        if result['pixels_over_two']>307 or result['mean_channel_error']>0.10:
            report['failures'].append(result['shape']+'/'+result['case'])
    negative=compare(native/'material-flat-vertex-top.ppm',native/'material-flat-identity-top.ppm')
    assert negative['pixels_over_two']>20000,'Material negative control must be visible'
    report['negative_control']=negative
    (root/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    assert not report['failures'],report['failures']
    print('Material renderer:',len(report['cases']),'native/engine pixel comparisons passed')


if __name__=='__main__': main()

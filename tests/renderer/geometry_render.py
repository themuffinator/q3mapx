"""Quake3e render-target comparisons with an original deterministic cgame fixture.

Requires an external GPL-compatible reference engine/source and legally available
Quake III assets. Never captures the OS display or accesses user input devices.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from integration import Bsp


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pixels(path):
    data = path.read_bytes()
    width, height = struct.unpack_from('<HH', data, 12)
    assert data[1:3] == b'\0\2' and data[16] == 24
    image = data[18+data[0]:]
    assert len(image) == width*height*3
    return (width, height, data[17]), image


def compare(a, b):
    size, before = pixels(a)
    other_size, after = pixels(b)
    assert size == other_size
    different, maximum, total = 0, 0, 0
    for i in range(0, len(before), 3):
        errors = [abs(before[i+c]-after[i+c]) for c in range(3)]
        largest = max(errors)
        different += largest != 0
        maximum = max(maximum, largest)
        total += sum(errors)
    return {'different_pixels': different, 'max_channel_error': maximum,
            'mean_channel_error': total/len(before), 'pixels': len(before)//3}


def draw_counts(log, label):
    # Last complete r_speeds line immediately preceding the registered capture.
    section = log.split('q3mapx_capture_'+label+'\n', 1)[1].split('Wrote screenshots/'+label+'.tga', 1)[0]
    matches = re.findall(r'(\d+) verts (\d+)/(\d+) tris', section)
    assert matches, 'Missing renderer triangle counters for '+label
    return dict(zip(('vertices', 'triangles', 'pass_triangles'), map(int, matches[-1])))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, required=True)
    parser.add_argument('--engine-source', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True, help='Read-only Quake III asset directory (contains default.cfg and vm/)')
    parser.add_argument('--fixture', type=Path, required=True, help='Completed geometry_optimize ambient fixture directory')
    parser.add_argument('--work-dir', type=Path, required=True)
    args = parser.parse_args()
    assert sys.platform.startswith('linux'), 'Use Linux SDL offscreen, never a desktop display'
    engine, source, assets, fixture, root = (getattr(args, key).resolve() for key in ('engine','engine_source','assets','fixture','work_dir'))
    root.mkdir(parents=True, exist_ok=True)
    library = root/'cgamex86_64.so'
    subprocess.run(['gcc','-shared','-fPIC','-std=c99','-O2','-I'+str(source/'code'),
                    str(Path(__file__).with_name('geometry_cgame.c')),'-o',str(library),'-lm'], cwd=root, check=True)
    original = fixture/'baseq3/maps/fixture.bsp'
    optimized = fixture/'optimized-1.bsp'
    changed = bytearray(optimized.read_bytes()); bsp = Bsp(optimized)
    shaders = bsp.lump(1)
    modified = set()
    for p in range(0, len(bsp.lump(13)), 104):
        shader, _, _, first, count = struct.unpack_from('<5i', bsp.lump(13), p)
        if shaders[shader*72:shader*72+64].split(b'\0')[0] != b'textures/q3mapx/grid': continue
        for vertex in range(first, first+count):
            if vertex in modified: continue
            offset = bsp.lumps[10][0]+vertex*44+8
            struct.pack_into('<f', changed, offset, struct.unpack_from('<f', changed, offset)[0]+16)
            modified.add(vertex)
    assert modified
    negative = root/'negative-control.bsp'; negative.write_bytes(changed)
    cameras = [('near','0 -220 208 35 90 90'), ('oblique','210 -170 224 28 140 100'),
               ('top','0 0 224 89.5 20 85'), ('grazing','-224 -100 145 4 24 110'), ('far','-220 -220 224 18 45 70')]
    lights = [('baked','0 0 0 0 1 0.4 0.2'), ('center','0 0 180 180 0.9 0.4 0.2'),
              ('edge','-130 70 130 80 0.3 1 0.4'), ('below','0 0 32 150 0.2 0.4 1')]
    cases = [(camera+'-'+light, position, lighting) for camera, position in cameras for light, lighting in lights]
    profiles = [('classic',0,0,0), ('vbo-perpixel',1,1,0), ('vertex-light',0,0,1)]
    results, failures, counters = [], [], {}
    # One-quantum channel differences over at most 0.1% of pixels are the
    # predeclared finite-raster tolerance; self-repeat must be byte-identical.
    for name, vbo, dynamic, vertex in profiles:
        variants = [('original', original), ('optimized', optimized)]
        if name == 'classic': variants += [('repeat', original), ('negative', negative)]
        for variant, bsp_path in variants:
            directory = root/f'{name}-{variant}'; game = directory/'baseq3'
            for subdir in ('scripts','textures'):
                shutil.copytree(fixture/'baseq3'/subdir, game/subdir, dirs_exist_ok=True)
            (game/'maps').mkdir(parents=True, exist_ok=True)
            shutil.copyfile(bsp_path, game/'maps/fixture.bsp')
            shutil.copyfile(library, game/library.name)
            selected = cases if variant != 'negative' else cases[:1]
            commands = ['wait 45']
            for label, camera, light in selected:
                commands += [f'set q3mapx_camera "{camera}"', f'set q3mapx_light "{light}"',
                             f'echo q3mapx_capture_{label}', 'wait 5', f'screenshot {label}', 'wait 3']
            commands += ['quit']
            (game/'q3mapx_capture.cfg').write_text('\n'.join(commands)+'\n')
            options = {'fs_basepath':str(assets.parent),'fs_basegame':assets.name,'fs_homepath':str(directory),'fs_game':'baseq3',
                       'r_fullscreen':'0','r_mode':'-1','r_customwidth':'640','r_customheight':'480',
                       'in_mouse':'0','in_joystick':'0','in_nograb':'1','s_initsound':'0',
                       'vm_cgame':'0','vm_game':'2','vm_ui':'2','sv_pure':'0','bot_enable':'0',
                       'r_vbo':str(vbo),'r_dlightMode':str(dynamic),'r_vertexLight':str(vertex),
                       'r_speeds':'1','r_gamma':'1','r_finish':'1','con_notifytime':'0',
                       'com_fixedtime':'16','com_maxfps':'125','com_maxfpsUnfocused':'125'}
            command = [str(engine), '+safe']
            for key, value in options.items(): command += ['+set', key, value]
            command += ['+devmap','fixture','+exec','q3mapx_capture.cfg']
            env = os.environ.copy()
            env.update(SDL_VIDEODRIVER='offscreen',SDL_AUDIODRIVER='dummy',LIBGL_ALWAYS_SOFTWARE='1',TMPDIR=str(root))
            result = subprocess.run(command,cwd=root,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=60)
            (directory/'engine.log').write_bytes(result.stdout)
            log = result.stdout.decode(errors='replace')
            assert result.returncode == 0 and 'q3mapx deterministic renderer fixture active' in log, log[-2500:]
            assert 'SDL using driver "offscreen"' in log and 'GL_RENDERER: llvmpipe' in log
            for label, _, _ in selected:
                assert f'Wrote screenshots/{label}.tga' in log and (game/f'screenshots/{label}.tga').is_file()
                counters[name, variant, label] = draw_counts(log, label)
            (directory/'command.json').write_text(json.dumps(command,indent=2)+'\n')
            print(name, variant, len(selected), 'engine render targets', flush=True)
        for label, _, _ in cases:
            before = root/f'{name}-original/baseq3/screenshots/{label}.tga'
            after = root/f'{name}-optimized/baseq3/screenshots/{label}.tga'
            metrics = compare(before,after)
            before_counts, after_counts = counters[name,'original',label], counters[name,'optimized',label]
            # Every view includes the complete generated grid. Verify actual
            # submitted triangles, not only the BSP's serialized index counts.
            assert before_counts['triangles']-after_counts['triangles'] == 368
            record = {'profile':name,'case':label,'original_sha256':sha(before),'optimized_sha256':sha(after),
                      'original_draw':before_counts,'optimized_draw':after_counts, **metrics}
            if metrics['max_channel_error'] > 1 or metrics['different_pixels'] > metrics['pixels']*0.001: failures.append(record)
            results.append(record)
            if name == 'classic': assert before.read_bytes() == (root/f'{name}-repeat/baseq3/screenshots/{label}.tga').read_bytes(), 'Unstable repeated renderer capture'
    negative_metrics = compare(root/'classic-original/baseq3/screenshots/near-baked.tga',root/'classic-negative/baseq3/screenshots/near-baked.tga')
    assert negative_metrics['different_pixels'] > 1000, 'Render fixture failed to observe changed grid geometry'
    report = {'engine_sha256':sha(engine),'cgame_sha256':sha(library),'input_sha256':sha(original),'optimized_sha256':sha(optimized),
              'capture':'Engine screenshot command, SDL offscreen, windowed 640x480, llvmpipe; no desktop or input',
              'tolerance':{'max_channel_error':1,'max_changed_fraction':0.001},
              'negative_control':negative_metrics,'results':results,'failures':failures}
    (root/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    assert not failures, f'{len(failures)} render comparisons exceeded the predeclared tolerance; see validation.json'
    print('60 renderer comparisons, 20 exact repeats and visible-geometry negative control passed')


if __name__ == '__main__':
    main()

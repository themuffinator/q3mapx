"""CLI argument bounds, profiling, and real VIS worker parity. GPL-3.0-or-later."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from fixtures import create_fixture
from integration import Bsp, run


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    source = create_fixture(root, dense=True, grid=5)
    base = ['-game', 'quake3', '-fs_basepath', root]
    for option, values in {
        '-threads': ['0', '-1', '1025', '999999999999', '4oops'],
        '-subdivisions': ['0', '-9', 'nan', '1025'],
        '-maxmapdrawsurfs': ['-1', '0', '2147483647', 'x'],
    }.items():
        for value in values:
            result = subprocess.run([str(exe), *map(str, base), option, value, str(source)],
                                    cwd=root, capture_output=True, timeout=10)
            assert result.returncode != 0 and b'ERROR' in result.stdout, (option, value, result.stdout)
    run(exe, [*base, '-threads', 'auto', '-meta', source], root, 'bsp')
    for option,value in [('-super','999999'),('-samples','-5'),('-lightmapsize','2147483647'),
                         ('-lightmapsearchpower','-1'),('-lightmapsearchpower','32'),('-samplescale','9999999'),
                         ('-bounce','-1'),('-gamma','nan'),('-scale','inf'),('-dirtdepth','1e999'),
                         ('-light-backend','unknown'),('-gpu-device','-1'),('-compute-report','output.bsp')]:
        result=subprocess.run([str(exe),*map(str,base),'-light',option,value,str(source)],cwd=root,capture_output=True,timeout=10)
        assert result.returncode!=0 and b'ERROR' in result.stdout,(option,value,result.stdout[-2000:])
    for options in [['-light','-super','8','-lightmapsize','2048'],['-light','-lightmapsearchpower','20'],['-profile','x'*1001]]:
        result=subprocess.run([str(exe),*map(str,base),*options,str(source)],cwd=root,capture_output=True,timeout=10)
        assert result.returncode!=0 and b'ERROR' in result.stdout,options
    path_checks=[]
    for length in (999,1000,1001,1020,2048):
        result=subprocess.run([str(exe),*map(str,base),'-threads','1','-info','x'*length],
                              cwd=root,capture_output=True,timeout=10)
        output=result.stdout+result.stderr
        assert result.returncode==1,(length,result.returncode,output[-2000:])
        assert b'AddressSanitizer' not in output and b'runtime error:' not in output
        guarded=b'Command-line argument exceeds supported 1000-byte length' in output
        assert guarded==(length>1000),(length,output[-2000:])
        path_checks.append({'argument_bytes':length,'exit_code':result.returncode,
                            'global_argument_guard':guarded,'sanitizer_failure':False})
    (root/'info-path-bounds.json').write_text(json.dumps(path_checks,indent=2)+'\n',encoding='utf-8')
    visibility = []
    for workers in (1, 4, 70):
        profile = root / f'profile-{workers}.json'
        run(exe, [*base, '-threads', workers, '-profile', profile, '-vis', '-saveprt', source],
            root, f'vis-{workers}')
        report = json.loads(profile.read_text())
        assert report['requested_workers'] == workers and report['exit_code'] == 0
        assert report['passes'] and report['total_seconds'] > 0
        for item in report['passes']:
            assert item['name'] != 'parallel' and 0 <= item['workers'] <= workers
            assert item['seconds'] >= 0 and item['grain'] > 0
        visibility.append(Bsp(source.with_suffix('.bsp')).lump(16))
    assert visibility[0] == visibility[1] == visibility[2], 'Worker-dependent visibility data'
    # Captured from NRC 8216133, built with the same synthetic grid=5 fixture.
    # This checks the actual visibility output, including the high half of bitsets.
    assert hashlib.sha256(visibility[0]).hexdigest()=='4271c3a613b1741da612de2bc9c874b814744bb39cb4d3933d54df2995a0af1f'
    for mode,expected in {
        '-nopassage':'9c6f3761c775c73615c5f6f4b1e24402887dd0b6e457953a885db3a74c0de8c9',
        '-passageOnly':'55646326c04e305014372daf74102984c01cdacf99f66200b2ae726f5ca67c2a',
        '-fast':'4e0df2074488030ab5a9f12bdfeed241fbe9c9f7a4a8ecda9673fb70f1a54f8b',
    }.items():
        run(exe,[*base,'-threads',1,'-vis','-saveprt',mode,source],root,'reference'+mode)
        assert hashlib.sha256(Bsp(source.with_suffix('.bsp')).lump(16)).hexdigest()==expected,mode
    print('Numeric bounds, JSON profiles and VIS parity at 1/4/70 workers passed')


if __name__ == '__main__':
    main()

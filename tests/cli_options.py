"""CLI argument bounds, profiling, and real VIS worker parity. GPL-3.0-or-later."""
import argparse
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
                         ('-bounce','-1'),('-gamma','nan'),('-scale','inf'),('-dirtdepth','1e999')]:
        result=subprocess.run([str(exe),*map(str,base),'-light',option,value,str(source)],cwd=root,capture_output=True,timeout=10)
        assert result.returncode!=0 and b'ERROR' in result.stdout,(option,value,result.stdout[-2000:])
    for options in [['-light','-super','8','-lightmapsize','2048'],['-light','-lightmapsearchpower','20'],['-profile','x'*1001]]:
        result=subprocess.run([str(exe),*map(str,base),*options,str(source)],cwd=root,capture_output=True,timeout=10)
        assert result.returncode!=0 and b'ERROR' in result.stdout,options
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
    print('Numeric bounds, JSON profiles and VIS parity at 1/4/70 workers passed')


if __name__ == '__main__':
    main()

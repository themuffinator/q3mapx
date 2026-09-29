"""Indexed/reference minimap parity, determinism and argument safety. GPL-3.0-or-later."""
import argparse
from pathlib import Path
import subprocess
from fixtures import create_fixture
from integration import run


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    source = create_fixture(root, dense=True, grid=7)
    base = ['-game', 'quake3', '-fs_basepath', root]
    run(exe, [*base, '-threads', 4, '-meta', source], root, 'bsp')
    for options in ([], ['-samples',4], ['-random',8,'-seed',19], ['-samples',4,'-border',0.1,'-sharpen',0.5]):
        results=[]
        for backend, workers in [('reference',1),('cpu',1),('cpu',4)]:
            output=root/'result.tga'
            run(exe,[*base,'-threads',workers,'-minimap','-backend',backend,'-size',127,'-o',output,*options,source],
                root,f'minimap-{backend}-{workers}')
            results.append(output.read_bytes())
        assert results[0] == results[1] == results[2], options
    for option, value in [('-size','-1'),('-size','8193'),('-samples','0'),('-random','-4'),
                          ('-border','0.5'),('-boost','nan'),('-sharpen','inf'),('-backend','typo')]:
        result=subprocess.run([str(exe),*map(str,base),'-minimap',option,value,str(source)],cwd=root,capture_output=True,timeout=10)
        assert result.returncode != 0 and b'ERROR' in result.stdout, (option,value)
    print('Reference/indexed/thread parity, deterministic random sampling and invalid options passed')


if __name__ == '__main__':
    main()

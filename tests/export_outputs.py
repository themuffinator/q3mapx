"""Real mesh publication failures must preserve existing exports. GPL-3.0-or-later."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from fixtures import create_fixture
from integration import run


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--work-dir',type=Path,required=True)
    args=parser.parse_args()
    exe,root=args.compiler.resolve(),args.work_dir.resolve()
    source=create_fixture(root)
    base=['-game','quake3','-fs_basepath',root,'-threads',4]
    run(exe,[*base,'-meta',source],root,'bsp')
    original=source.with_suffix('.bsp').read_bytes()
    checked=[]

    def input_for(name):
        path=root/(name+'.bsp'); path.write_bytes(original)
        return path

    def export(path,format,label,success=False,**options):
        result=subprocess.run([str(exe),*map(str,base),'-convert','-format',format,str(path)],
                              cwd=root,capture_output=True,timeout=60,**options)
        output=result.stdout+result.stderr
        (root/(label+'.log')).write_bytes(output)
        assert result.returncode==(0 if success else 1),(label,result.returncode,output[-4000:])
        if not success: assert b'ERROR' in output,(label,output[-4000:])
        assert path.read_bytes()==original,label
        assert not list(root.rglob('*.q3mapx-*.tmp')),(label,'Staged files were not cleaned up')
        checked.append(label)
        return output

    # The old writer truncated OBJ before discovering that MTL could not open.
    path=input_for('blocked-companion')
    obj,mtl=path.with_suffix('.obj'),path.with_suffix('.mtl')
    obj.write_bytes(b'keep the previous mesh')
    mtl.mkdir(exist_ok=True); (mtl/'preserved').write_bytes(b'keep directory contents')
    export(path,'obj','companion-directory')
    assert obj.read_bytes()==b'keep the previous mesh'
    assert (mtl/'preserved').read_bytes()==b'keep directory contents'

    # Exercise successful replacement of both companions and a single ASE file.
    path=input_for('replace')
    for suffix in ('.obj','.mtl','.ase'): path.with_suffix(suffix).write_bytes(b'previous export')
    export(path,'obj','replace-pair',True)
    export(path,'ase','replace-ase',True)
    assert b'mtllib replace.mtl' in path.with_suffix('.obj').read_bytes()
    assert b'newmtl' in path.with_suffix('.mtl').read_bytes()
    assert b'*3DSMAX_ASCIIEXPORT' in path.with_suffix('.ase').read_bytes()

    if os.name=='nt':
        import ctypes
        from ctypes import wintypes
        kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p,
                                    wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
        kernel.CreateFileW.restype=wintypes.HANDLE
        kernel.CloseHandle.argtypes=[wintypes.HANDLE]; kernel.CloseHandle.restype=wintypes.BOOL
        # Deny rename/delete on OBJ while allowing reads/writes. MTL publishes
        # first; the later Windows sharing violation must roll that change back.
        for existing in (False,True):
            path=input_for('locked-'+str(existing))
            obj,mtl=path.with_suffix('.obj'),path.with_suffix('.mtl')
            obj.write_bytes(b'previous locked mesh')
            if existing: mtl.write_bytes(b'previous material')
            elif mtl.is_file(): mtl.unlink()
            assert existing or not mtl.exists()
            handle=kernel.CreateFileW(str(obj),0x80000000,1|2,None,3,0,None)
            assert handle not in (None,ctypes.c_void_p(-1).value),ctypes.get_last_error()
            try:
                output=export(path,'obj','rollback-material-'+str(existing))
                assert b'Cannot replace' in output,output[-4000:]
            finally:
                assert kernel.CloseHandle(handle)
            assert obj.read_bytes()==b'previous locked mesh'
            assert mtl.read_bytes()==b'previous material' if existing else not mtl.exists()
            # A successful retry must work after the sharing lock is released.
            export(path,'obj','retry-unlocked-'+str(existing),True)
    else:
        import resource
        import signal
        def bounded_output():
            signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
            _,hard=resource.getrlimit(resource.RLIMIT_FSIZE)
            resource.setrlimit(resource.RLIMIT_FSIZE,(16*1024,hard))
        for format in ('obj','ase'):
            path=input_for('write-limit-'+format)
            suffixes=('.obj','.mtl') if format=='obj' else ('.ase',)
            for suffix in suffixes: path.with_suffix(suffix).write_bytes(b'previous '+suffix.encode())
            output=export(path,format,'write-limit-'+format,preexec_fn=bounded_output)
            assert b'Cannot finish output' in output,output[-4000:]
            for suffix in suffixes: assert path.with_suffix(suffix).read_bytes()==b'previous '+suffix.encode()

    # Links are rejected rather than followed into the source or another file.
    link_source=input_for('linked-output')
    link=link_source.with_suffix('.obj')
    try:
        if not link.is_symlink(): link.symlink_to(link_source)
    except OSError as error:
        if os.name!='nt' or error.winerror not in (5,1314): raise
        print('SKIP: creating a symbolic link requires Windows privileges')
    else:
        export(link_source,'obj','reject-linked-output')
        assert link.is_symlink() and link_source.read_bytes()==original
        assert not link_source.with_suffix('.mtl').exists()

    report={'schema_version':1,'compiler_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
            'platform':os.name,'checks':checked,'result':'passed'}
    (root/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('Checked mesh writes, original preservation, rollback/retry and source protection passed')


if __name__=='__main__': main()

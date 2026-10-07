"""Create an auditable portable MinGW/Qt release under build/package.

Uses the installed MSYS2 package database for DLL ownership and license notices.
No system DLLs or GPU drivers are redistributed. SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]


def run(arguments, *, env=None):
    try:
        result=subprocess.run(list(map(str,arguments)),cwd=ROOT,env=env,capture_output=True,text=True,errors='replace')
    except OSError as error:
        raise RuntimeError(f'Cannot execute {arguments[0]}: {error}') from error
    if result.returncode:
        raise RuntimeError(f'{arguments[0]} failed ({result.returncode}):\n{result.stdout}\n{result.stderr}')
    return result.stdout


def digest(path):
    checksum=hashlib.sha256()
    with path.open('rb') as file:
        for chunk in iter(lambda:file.read(1024*1024),b''): checksum.update(chunk)
    return checksum.hexdigest()


def check_version(executable, product, expected, env):
    output=run([executable,'--version'],env=env).strip()
    if not re.fullmatch(re.escape(product+' '+expected)+r'(?: \(NRC [^\r\n]+\))?',output):
        raise RuntimeError(f'Rebuild {executable}: expected {product} {expected}, received {output!r}')


def write_json(path,value):
    path.write_text(json.dumps(value,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')


def metadata(path):
    fields={}; key=None
    for line in path.read_text(encoding='utf-8').splitlines():
        if line.startswith('%') and line.endswith('%'): key=line.strip('%'); fields[key]=[]
        elif line and key: fields[key].append(line)
    return fields


def package_database(msys):
    owners={}; packages={}
    for directory in (msys/'var/lib/pacman/local').iterdir():
        if not (directory/'desc').is_file() or not (directory/'files').is_file(): continue
        fields=metadata(directory/'desc')
        name=fields['NAME'][0]
        entries=metadata(directory/'files').get('FILES',[])
        packages[name]={'name':name,'version':fields['VERSION'][0],'base':(fields.get('BASE') or [name])[0],
                        'homepage':(fields.get('URL') or [''])[0],'license_expression':'; '.join(fields.get('LICENSE',[])),
                        'files':entries}
        for entry in entries:
            if not entry.endswith('/'): owners[entry.lower()]=name
    return owners,packages


def make_zip(path, files, epoch):
    timestamp=datetime.fromtimestamp(max(epoch,315532800),timezone.utc).timetuple()[:6]
    with zipfile.ZipFile(path,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for source,name in sorted(files,key=lambda item:item[1]):
            info=zipfile.ZipInfo(name,date_time=timestamp); info.compress_type=zipfile.ZIP_DEFLATED
            info.external_attr=0o100644 << 16
            archive.writestr(info,source.read_bytes())


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/release')
    parser.add_argument('--msys-root',type=Path,default=Path('C:/msys64'))
    parser.add_argument('--prefix',default='mingw64',choices=['mingw64','ucrt64'])
    from release import read_version
    version=read_version(ROOT)
    parser.add_argument('--name',default=f'q3mapx-{version}-windows-x64')
    parser.add_argument('--cli-only',action='store_true')
    parser.add_argument('--allow-dirty',action='store_true',help='Include current uncommitted source in a local development snapshot')
    parser.add_argument('--fetch-dependency-sources',action='store_true',help='Download exact MSYS2 source packages beside the portable archive')
    parser.add_argument('--source-index',type=Path,help='Use a saved MSYS2 source index instead of retrieving the current index')
    args=parser.parse_args()
    if os.name!='nt': parser.error('This packager requires Windows and an installed MSYS2 toolchain')
    if not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9._-]*',args.name): parser.error('Name must be a simple directory name')
    build=args.build_dir.resolve(); msys=args.msys_root.resolve(); prefix=msys/args.prefix
    output=(ROOT/'build/package'/args.name).resolve()
    if not output.is_relative_to((ROOT/'build/package').resolve()): parser.error('Output must remain in build/package')
    if output.exists(): parser.error(f'Refusing to overwrite existing package: {output}')
    revision=run(['git','rev-parse','HEAD']).strip()
    status=run(['git','status','--porcelain'])
    if status and not args.allow_dirty: parser.error('Commit the source first, or use --allow-dirty for a local development snapshot')
    epoch=int(os.environ.get('SOURCE_DATE_EPOCH',run(['git','show','-s','--format=%ct','HEAD']).strip()))
    env=os.environ.copy(); env['PATH']=str(prefix/'bin')+os.pathsep+env.get('PATH','')
    for key in ('QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH','QML2_IMPORT_PATH'): env.pop(key,None)
    compiler=build/'bin/q3mapx.exe'; gui=build/'bin/q3mapx-workbench.exe'
    if not compiler.is_file() or (not args.cli_only and not gui.is_file()): parser.error('Build the required release executables first')
    check_version(compiler,'q3mapx',version,env)
    if not args.cli_only: check_version(gui,'q3mapx-workbench',version,env)
    output.mkdir(parents=True)
    run([prefix/'bin/cmake.exe','--install',build,'--prefix',output],env=env)
    binary=output/'bin'
    if args.cli_only and (binary/'q3mapx-workbench.exe').exists(): (binary/'q3mapx-workbench.exe').unlink()
    origins={}
    if not args.cli_only:
        deployed=run([prefix/'bin/windeployqt6.exe','--release','--no-translations','--no-opengl-sw',
                      '--no-system-d3d-compiler','--no-system-dxc-compiler','--no-compiler-runtime','--no-patchqt',
                      '--no-plugins','--dir',binary,binary/'q3mapx-workbench.exe'],env=env)
        (output/'qt-deployment.log').write_text(deployed,encoding='utf-8')
        plugin_root=Path(run([prefix/'bin/qtpaths6.exe','--query','QT_INSTALL_PLUGINS'],env=env).strip())
        for name in ('platforms/qwindows.dll','platforms/qoffscreen.dll','styles/qmodernwindowsstyle.dll',
                     'imageformats/qjpeg.dll','imageformats/qgif.dll','imageformats/qico.dll'):
            source=plugin_root/name; target=binary/name
            target.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(source,target)
            origins[target.relative_to(output).as_posix()]=source
        (binary/'qt.conf').write_text('[Paths]\nPlugins=.\n',encoding='utf-8')

    available={file.name.lower():file for file in (prefix/'bin').glob('*.dll')}
    system=Path(os.environ['SystemRoot'])/'System32'
    pending=list(binary.rglob('*.dll'))+list(binary.glob('*.exe')); visited=set(); imports={}
    while pending:
        file=pending.pop(); relative=file.relative_to(output).as_posix()
        if relative in visited: continue
        visited.add(relative)
        if file.suffix.lower()=='.dll' and relative not in origins:
            if file.name.lower() not in available: raise RuntimeError(f'Unknown deployed DLL origin: {file}')
            origins[relative]=available[file.name.lower()]
        dependencies=re.findall(r'DLL Name:\s*(\S+)',run([prefix/'bin/objdump.exe','-p',file],env=env))
        imports[relative]=dependencies
        for name in dependencies:
            source=available.get(name.lower()); destination=binary/name
            if source:
                if not destination.exists(): shutil.copy2(source,destination)
                origins[destination.relative_to(output).as_posix()]=source
                pending.append(destination)
            elif name.lower().startswith(('api-ms-win-','ext-ms-win-')) or (system/name).is_file():
                continue
            else: raise RuntimeError(f'Missing runtime dependency {name}, required by {relative}')

    owners,installed=package_database(msys)
    if args.source_index:
        source_index=args.source_index.read_text(encoding='utf-8')
    else:
        with urllib.request.urlopen('https://repo.msys2.org/mingw/sources/',timeout=60) as response:
            source_index=response.read().decode('utf-8')
        (ROOT/'build/package/msys-source-index.html').write_text(source_index,encoding='utf-8')
    source_names=set(re.findall(r'href="([^"/]+\.src\.tar\.(?:zst|xz|gz))"',source_index))
    packages={}; files=[]
    notice_dir=output/'licenses/runtime'; notice_dir.mkdir(parents=True)
    for relative,source in sorted(origins.items()):
        name=owners.get(source.relative_to(msys).as_posix().lower())
        if not name: raise RuntimeError(f'No package owns {source}')
        package=installed[name]
        if name not in packages:
            notices=[entry for entry in package['files'] if not entry.endswith('/') and
                     ('/share/licenses/' in entry or ('/share/' in entry and
                      Path(entry).name.lower() in ('license','license.txt','copying','copying.lib','copyright')))]
            if not notices: raise RuntimeError(f'Package has no license notices: {name}')
            for entry in notices:
                target=notice_dir/name/entry.split('/share/',1)[1]
                target.parent.mkdir(parents=True,exist_ok=True); shutil.copy2(msys/entry,target)
            item={key:value for key,value in package.items() if key!='files'}
            source_base=f"{item['base']}-{item['version'].split(':')[-1]}.src.tar."
            source_name=next((source_base+ext for ext in ('zst','xz','gz') if source_base+ext in source_names),None)
            if not source_name: raise RuntimeError(f"No exact source archive listed for {name} {item['version']}")
            item['source_url']='https://repo.msys2.org/mingw/sources/'+source_name
            item['recipe_url']=f"https://github.com/msys2/MINGW-packages/tree/master/{item['base']}"
            item['notices']=[str(path.relative_to(output)).replace('\\','/') for path in sorted((notice_dir/name).rglob('*')) if path.is_file()]
            packages[name]=item
        files.append({'path':relative,'sha256':digest(output/relative),'package':name})

    shutil.copy2(ROOT/'README.md',output/'README.md')
    shutil.copytree(ROOT/'docs',output/'docs')
    for name in ('COPYING','GPL','LGPL','LICENSE','CONTRIBUTORS','VERSION','CHANGELOG.md'): shutil.copy2(ROOT/name,output/name)
    # Ship precisely the current project source, including new local files in an
    # explicitly requested dirty snapshot, without build products or git metadata.
    source_paths=run(['git','ls-files','--cached','--others','--exclude-standard','-z']).split('\0')
    sources=[]
    for name in sorted(set(source_paths)):
        if not name: continue
        source=ROOT/name
        if not source.is_file(): continue
        if source.is_symlink() or not source.resolve().is_relative_to(ROOT): raise RuntimeError(f'Unsafe source path: {source}')
        sources.append((source,'q3mapx/'+name.replace('\\','/')))
    make_zip(output/'q3mapx-source.zip',sources,epoch)
    for exe in binary.glob('*.exe'):
        files.append({'path':exe.relative_to(output).as_posix(),'sha256':digest(exe),'package':'q3mapx'})
    manifest={'schema_version':1,'project':'q3mapx','version':version,'revision':revision,'dirty_source_snapshot':bool(status),
              'source_archive_sha256':digest(output/'q3mapx-source.zip'),
              'source_date_epoch':epoch,'toolchain_prefix':args.prefix,'files':files,'imports':imports,
              'packages':list(packages.values()),'dependency_sources_downloaded':False}
    if args.fetch_dependency_sources:
        source_dir=ROOT/'build/package/dependency-sources'; source_dir.mkdir(parents=True,exist_ok=True)
        for package in packages.values():
            url=package['source_url']; target=source_dir/url.rsplit('/',1)[-1]
            if not target.exists():
                print(f"Downloading source: {package['name']}",flush=True)
                temporary=target.with_suffix(target.suffix+'.part')
                try:
                    with urllib.request.urlopen(url,timeout=90) as response, temporary.open('wb') as file:
                        shutil.copyfileobj(response,file)
                    temporary.replace(target)
                finally:
                    if temporary.exists(): temporary.unlink()
            package['source_archive']=target.name; package['source_sha256']=digest(target)
        manifest['dependency_sources_downloaded']=True
    write_json(output/'runtime-manifest.json',manifest)
    lines=['# Runtime credits and corresponding source','',
           'q3mapx is GPL-3.0-or-later. Its exact source is in `q3mapx-source.zip`.',
           'Runtime libraries are unmodified shared libraries from MSYS2. Their notices',
           'are in `licenses/runtime`; Qt uses its open-source license terms. These DLLs',
           'can be replaced with ABI-compatible builds. GPU drivers and Windows system',
           'libraries are supplied by the operating system, not this package.','',
           'This software is based in part on the work of the Independent JPEG Group.','',
           'The manifest records exact installed versions, file hashes and source packages.',
           'For offline redistribution, keep the downloaded `dependency-sources` folder',
           'alongside the binary archive. The packaging command can fetch those sources.',
           'Build recipes inside the versioned source packages are authoritative; the',
           'recipe links below are navigation links to the evolving MSYS2 repository.','']
    for package in packages.values():
        lines += [f"- **{package['name']} {package['version']}** — {package['license_expression']}",
                  f"  [Project]({package['homepage']}) · [Exact source package]({package['source_url']}) · [Build recipes]({package['recipe_url']})"]
    (output/'RUNTIME-CREDITS.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    archive=Path(str(output)+'.zip')
    if archive.exists(): raise RuntimeError(f'Refusing to replace archive {archive}')
    make_zip(archive,[(file,args.name+'/'+file.relative_to(output).as_posix()) for file in output.rglob('*') if file.is_file()],epoch)
    archive.with_suffix('.zip.sha256').write_text(digest(archive)+'  '+archive.name+'\n',encoding='ascii')
    print(json.dumps({'directory':str(output),'archive':str(archive),'runtime_packages':len(packages),
                      'dependency_dlls':len(origins),'sha256':digest(archive)},indent=2))


if __name__=='__main__':
    try: main()
    except Exception as error:
        print(f'Packaging failed: {error}',file=sys.stderr); sys.exit(1)

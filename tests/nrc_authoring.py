"""Run the optional NRC native authoring harness in an isolated portable profile.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
from xml.sax.saxutils import escape, quoteattr

from fixtures import box, create_fixture
from brush_input import adapt
from surface_density import PROJECTION, authored_brush, authored_patch, extras
from integration import run


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--editor-dir',type=Path,required=True)
    p.add_argument('--compiler',type=Path,required=True)
    p.add_argument('--work-dir',type=Path,required=True)
    a=p.parse_args()
    project=Path(__file__).resolve().parent.parent
    editor,root,compiler=a.editor_dir.resolve(),a.work_dir.resolve(),a.compiler.resolve()
    assert compiler.is_file(), 'Compiler executable does not exist'
    # This harness must never write profiles into a real editor installation.
    allowed=(project/'build',project/'.agents/tmp')
    assert any(editor.is_relative_to(base) for base in allowed), 'Use an isolated editor build inside this project'
    assert any(root.is_relative_to(base) for base in allowed), 'Use a project-local test output directory'
    install=editor/'install'
    executable=install/('q3mapx-authoring-test.exe' if os.name=='nt' else 'q3mapx-authoring-test')
    assert executable.is_file()
    root.mkdir(parents=True,exist_ok=True)
    settings=install/'settings'
    marker=settings/'.q3mapx-test-profile'
    assert not settings.exists() or marker.is_file(), 'Refusing to overwrite an existing editor profile'
    settings.mkdir(exist_ok=True)
    marker.write_text('Disposable q3mapx authoring test profile\n',encoding='utf-8')
    engine=root/'engine'
    source=create_fixture(engine,patch=False)
    plain=source.read_text(encoding='utf-8')
    # Point-entity labels allocate GL textures in upstream NRC. Keep the native
    # parser fixture to a complete worldspawn graph for this no-GL harness and
    # append the generated point/door entities for the compiler check below.
    entity_start=plain.index('{\n"classname" "info_player_deathmatch"')
    world,entities=plain[:entity_start],plain[entity_start:]
    name='q3mapx-test.game'
    pack=install/'gamepacks'/name
    (pack/'baseq3').mkdir(parents=True,exist_ok=True)
    (pack/'default_build_menu.xml').write_text('<project version="2.0"/>\n',encoding='utf-8')
    games=install/'gamepacks/games'
    games.mkdir(parents=True,exist_ok=True)
    description={'name':'q3mapx authoring tests','type':'q3','basegame':'baseq3',
        'basegamename':'Generated test assets','unknowngamename':'Test mod',
        'enginepath_win32':engine.as_posix()+'/','enginepath_linux':engine.as_posix()+'/',
        'engine_win32':'unused.exe','engine_linux':'unused','entities':'quake3','shaders':'quake3',
        'brushtypes':'quake3','patchtypes':'quake3','entityclass':'quake3','entityclasstype':'def',
        'texturetypes':'tga','archivetypes':'pk3','modeltypes':'md3','maptypes':'mapq3'}
    (games/name).write_text('<game '+ ' '.join(k+'='+quoteattr(v) for k,v in description.items())+'/>\n',encoding='utf-8')
    (install/'RADIANT_MAJOR').write_text('6\n')
    (install/'RADIANT_MINOR').write_text('0\n')
    pref=settings/'1.6.0'
    (pref/name).mkdir(parents=True,exist_ok=True)
    for pid in (pref/'radiant.pid',pref/name/'radiant-game.pid'):
        assert not pid.is_symlink()
        if pid.is_file(): pid.unlink()
    def preferences(values):
        return '<qpref version="1.0">'+''.join('<epair name='+quoteattr(k)+'>'+escape(v)+'</epair>' for k,v in values.items())+'</qpref>\n'
    (pref/'global.pref').write_text(preferences({'gamefile':name,'gamePrompt':'false'}),encoding='utf-8')
    (pref/name/'local.pref').write_text(preferences({'EnginePath':engine.as_posix()+'/',
        'InstalledDevFilesPath':engine.as_posix()+'/','LoadLastMap':'false'}),encoding='utf-8')
    for style in PROJECTION:
        (root/('brush-'+style+'.txt')).write_text(authored_brush(box((-224,0,0),(-160,64,64)),style,[0,0,0,0,0,8]),encoding='utf-8')
        text=adapt(world,style).replace('"message" "q3mapx regression"\n',
            '"message" "q3mapx regression"\n'+(root/('brush-'+style+'.txt')).read_text()+authored_patch(12))
        (root/('map-'+style+'.map')).write_text(text,encoding='utf-8')
    (root/'patch.txt').write_text(authored_patch(12),encoding='utf-8')
    env=dict(os.environ,Q3MAPX_TEST_OUTPUT=str(root),QT_QPA_PLATFORM='offscreen',PYTHONDONTWRITEBYTECODE='1')
    if os.name=='nt': env['Q3MAPX_TEST_FONT']=str(Path(os.environ.get('WINDIR','C:/Windows'))/'Fonts/segoeui.ttf')
    result=subprocess.run([str(executable)],cwd=editor,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90)
    (root/'editor.log').write_bytes(result.stdout)
    assert result.returncode==0,(result.returncode,result.stdout[-5000:].decode(errors='replace'),
                                (pref/'radiant.log').read_text(encoding='utf-8',errors='replace')[-5000:])
    native=json.loads((root/'results.json').read_text())
    compiled=[]
    for style in PROJECTION:
        text=(root/('native-'+style+'.map')).read_text()+adapt(entities,style)
        source.write_text(text,encoding='utf-8')
        base=['-game','quake3','-fs_basepath',engine,'-fs_homepath',engine/'home','-threads',1]
        run(compiler,[*base,'-meta','-patchmeta',source],root,'compiler-'+style)
        rows=extras(source)
        assert {row['authoredSampleSize'] for row in rows.values() if row.get('authoredSampleSize')}=={8,12}
        assert all(row['sampleSize'] in (0,row['authoredSampleSize']) for row in rows.values() if row.get('authoredSampleSize'))
        compiled.append(style)
    native['compiler_roundtrips']=compiled
    (root/'results.json').write_text(json.dumps(native,indent=2)+'\n',encoding='utf-8')
    print(f"NRC authoring: {native['checks']} native checks; {len(compiled)} compiler round trips")


if __name__=='__main__': main()

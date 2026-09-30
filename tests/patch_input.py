"""MAP patch parsing boundaries, output preservation and valid-source parity.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from fixtures import create_fixture
from integration import run


def digest(data):
    return hashlib.sha256(data).hexdigest()


def payloads(data):
    count = 18 if data[:4] == b'RBSP' else 17
    assert data[:4] in (b'IBSP', b'RBSP')
    return [data[offset:offset+size] for offset, size in
            (struct.unpack_from('<2i',data,8+i*8) for i in range(count))]


def patch(width=3, height=3, *, header=None, metadata='', format_number=str):
    rows = []
    for x in range(width):
        row = []
        for y in range(height):
            values = (-192+128*x/(width-1), -192+128*y/(height-1),
                      16+48*(x%2)+32*(y%2), x/2, y/2)
            row.append('( '+' '.join(format_number(v) for v in values)+' )')
        rows.append('( '+' '.join(row)+' )')
    return '{\npatchDef2\n{\nq3mapx/stone\n( '+(header or f'{width} {height} 0 0 0')+' )\n(\n'+'\n'.join(rows)+'\n)\n'+metadata+'}\n}\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',required=True,type=Path)
    parser.add_argument('--reference',type=Path)
    parser.add_argument('--work-dir',required=True,type=Path)
    args = parser.parse_args()
    compiler, root = args.compiler.resolve(), args.work_dir.resolve()
    reference = args.reference.resolve() if args.reference else None
    root.mkdir(parents=True,exist_ok=True)
    directory = root/'native'
    source = create_fixture(directory,patch=False)
    plain = source.read_text(encoding='utf-8')
    marker = '"message" "q3mapx regression"\n'
    assert plain.count(marker) == 1

    def with_patch(text):
        return plain.replace(marker,marker+text).encode()

    base = ['-game','quake3','-fs_basepath',directory,'-fs_homepath',directory/'home','-threads',1]
    source.write_bytes(with_patch(patch()))
    run(compiler,[*base,'-meta',source],directory,'carrier')
    original_bsp = source.with_suffix('.bsp').read_bytes()
    original_srf = source.with_suffix('.srf').read_bytes()
    saved = {source.with_suffix('.bsp'):original_bsp, source.with_suffix('.srf'):original_srf}
    for extension in ('.prt','.lin','.reg','.obj','.mtl'):
        saved[source.with_suffix(extension)] = f'previous {extension} output\n'.encode()

    valid = []
    variants = [('minimum',patch()), ('wide',patch(31,3)), ('tall',patch(3,31)),
                ('maximum',patch(31,31)), ('rectangular',patch(5,7)),
                ('scientific',patch(header='+3e0 03.000 0 -2.5 +4e1',format_number=lambda v:f'{v:+.17e}')),
                ('exponent',patch(header='300e-2 .3e1 0 0 0')),
                ('long-integral',patch(header='3.'+'0'*1021+' 3 0 0 0')),
                ('metadata',patch(metadata='"compile-note" "ignored metadata"\n')),
                ('long-metadata',patch(metadata='"compile-note" "'+'x'*1023+'"\n')),
                ('comments',patch().replace('( 3 3 0 0 0 )','( 3 /* width */ 3 0 0 0 )'))]
    for game in ('quake3','ja'):
        game_base = ['-game',game,'-fs_basegame','baseq3',*base[2:]]
        for name, text in variants:
            source.write_bytes(with_patch(text))
            for options in ([],['-meta','-patchmeta']):
                label = game+'-'+name+('-meta' if options else '-ordinary')
                run(compiler,[*game_base,*options,source],directory,label)
                current = source.with_suffix('.bsp').read_bytes()
                lumps = payloads(current)
                stride = 148 if game=='ja' else 104
                assert any(struct.unpack_from('<i',lumps[13],offset+8)[0]==2
                           for offset in range(0,len(lumps[13]),stride)), label
                record = {'case':label,'lump_hashes':[digest(p) for p in lumps]}
                if reference:
                    run(reference,[*game_base,*options,source],directory,label+'-reference')
                    old = payloads(source.with_suffix('.bsp').read_bytes())
                    assert old==lumps, (label,'valid-source output changed')
                    record['reference_lump_parity'] = True
                valid.append(record)

    cases = []
    template = patch()
    for dimension, position in (('width',0),('height',1)):
        for value in ('-1','0','1','2','4','30','32','33','65536','2147483647','1e30',
                      '3.5','3.0000001','3.0000000000000000001','nan','inf','-inf','1e309','1e-400','3junk','0x3','+-3','""'):
            header=['3','3','0','0','0'];header[position]=value
            cases.append((f'{dimension}-{len(cases)}',patch(header=' '.join(header))))
    for value in ('junk','nan','1e309'):
        cases.append(('header-'+value,patch(header='3 3 '+value+' 0 0')))
    point='( -192.0 -192.0 16 0.0 0.0 )'
    assert point in template
    for axis in range(5):
        for value in ('nan','inf','1e309','7junk','1e-400','+-2'):
            fields=['-192.0','-192.0','16','0.0','0.0'];fields[axis]=value
            cases.append((f'coordinate-{axis}-{value}',template.replace(point,'( '+' '.join(fields)+' )',1)))
        for value in (('65537','-65537','1e38') if axis<3 else ('3.5e38','-3.5e38','1e-46')):
            fields=['-192.0','-192.0','16','0.0','0.0'];fields[axis]=value
            cases.append((f'coordinate-{axis}-{value}',template.replace(point,'( '+' '.join(fields)+' )',1)))
    cases.extend([('empty-material',template.replace('q3mapx/stone','""')),
                  ('missing-value',template.replace(point,'( -192 -192 16 0 )',1)),
                  ('extra-value',template.replace(point,'( -192 -192 16 0 0 7 )',1)),
                  ('missing-row',template.replace('( 3 3 0 0 0 )','( 5 3 0 0 0 )')),
                  ('missing-point',template.replace('( 3 3 0 0 0 )','( 3 5 0 0 0 )')),
                  ('extra-row',patch(5,3,header='3 3 0 0 0')),
                  ('extra-point',patch(3,5,header='3 3 0 0 0')),
                  ('nul-number',template.replace('( 3 3 0 0 0 )','( "3\0junk" 3 0 0 0 )'))])

    failed = []

    def reject(label, data, options=('-meta',), input_path=source):
        for destination, contents in saved.items(): destination.write_bytes(contents)
        input_path.write_bytes(data)
        result=subprocess.run([str(compiler),*map(str,[*base,*options,input_path])],
                              cwd=directory,capture_output=True,timeout=15)
        output=result.stdout+result.stderr
        (directory/(label+'.log')).write_bytes(output)
        assert result.returncode==1 and b'ERROR' in output, (label,result.returncode,output[-2500:])
        assert b'AddressSanitizer' not in output and b'runtime error:' not in output, label
        assert b'Invalid MAP patch' in output or b'Incomplete MAP patch' in output or b'line' in output.lower(), (label,output[-2500:])
        assert b'Allocation failed' not in output and b'Memory allocation failed' not in output, label
        assert input_path.read_bytes()==data
        for destination, contents in saved.items():
            if destination!=input_path:
                assert destination.read_bytes()==contents, (label,'changed previous output',destination)
        failed.append(label)

    for label,text in cases:
        reject(label,with_patch(text))
    # EOF at every token boundary inside a patch, with no closing world brace.
    # Begin after patchDef2 so the patch parser owns the incomplete construct.
    prefix=plain.split(marker)[0]+marker
    tokens=template.split()
    for count in range(2,len(tokens)):
        reject(f'truncated-{count}',(prefix+' '.join(tokens[:count])).encode())
    for size in (1023,1024,1025):
        for quoted in (False,True):
            reject(f'token-{size}-{quoted}',(prefix+'{ patchDef2 { q3mapx/stone ( '+('"' if quoted else '')+'9'*size).encode())
    for index,suffix in enumerate(('"','/*','/* *','/* unterminated')):
        reject(f'lexer-eof-{index}',(prefix+'{ patchDef2 { q3mapx/stone ( '+suffix).encode())

    bad = with_patch(patch(header='-1 3 0 0 0'))
    for label,options in [('nocurves',['-nocurves']),('onlyents',['-onlyents']),
                          ('light',['-light','-fast']),('convert-map',['-convert','-readmap','-format','map']),
                          ('convert-obj',['-convert','-readmap','-format','obj'])]:
        reject(label,bad,options)
    reject('region-source',bad,input_path=source.with_suffix('.reg'))
    temporary=directory/'editor-temp.map'
    temporary.write_bytes(bad)
    source.write_bytes(with_patch(patch()))
    reject('editor-temp',source.read_bytes(),options=('-tempname',temporary,'-meta'))
    assert temporary.read_bytes()==bad

    # A successful entity-only update must also retain geometry sidecars.
    for destination,contents in saved.items(): destination.write_bytes(contents)
    source.write_bytes(with_patch(patch()))
    run(compiler,[*base,'-onlyents',source],directory,'valid-onlyents')
    for destination,contents in saved.items():
        if destination.suffix!='.bsp': assert destination.read_bytes()==contents, destination
    after=payloads(source.with_suffix('.bsp').read_bytes())
    assert after[1:]==payloads(original_bsp)[1:]

    report={'compiler_sha256':digest(compiler.read_bytes()),
            'reference_sha256':digest(reference.read_bytes()) if reference else None,
            'valid_cases':valid,'rejected_cases':failed,'valid_onlyents_preserves_sidecars':True}
    (root/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(f'{len(valid)} native patch builds and {len(failed)} malformed sources/modes passed; valid-source parity and previous outputs preserved')


if __name__=='__main__':
    main()

"""Strict brush numbers, degenerate planes, texture overflow and native parity.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from fixtures import create_fixture
from integration import run
from patch_input import payloads


def digest(data):
    return hashlib.sha256(data).hexdigest()


def adapt(text, style):
    lines = text.splitlines()
    sides = [i for i, line in enumerate(lines) if line.startswith('( ')]
    for i in sides:
        fields = lines[i].split()
        if style == 'bp':
            lines[i] = ' '.join(fields[:15])+' ( ( 0.03125 0 0.25 ) ( 0 0.0625 -0.5 ) ) '+fields[15]+' 0 0 0'
        elif style == 'valve':
            lines[i] = ' '.join(fields[:16])+' [ 1 0 0 13 ] [ 0 1 0 -7 ] 23 0.5 0.75 0 0 0'
    if style == 'bp':
        for i in reversed(sides):
            if not lines[i+1].startswith('( '): lines[i+1:i+1] = ['}']
            if not lines[i-1].startswith('( '): lines[i:i] = ['brushDef', '{']
    return '\n'.join(lines)+'\n'


def edit_side(text, edits, side_number=0):
    lines = text.splitlines()
    index = [i for i, line in enumerate(lines) if line.startswith('( ')][side_number]
    fields = lines[index].split()
    for field, value in edits.items(): fields[field] = value
    lines[index] = ' '.join(fields)
    return '\n'.join(lines)+'\n'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', required=True, type=Path)
    p.add_argument('--reference', type=Path)
    p.add_argument('--work-dir', required=True, type=Path)
    a = p.parse_args()
    compiler, root = a.compiler.resolve(), a.work_dir.resolve()
    reference = a.reference.resolve() if a.reference else None
    directory = root/'native'
    source = create_fixture(directory, patch=False)
    plain = source.read_text(encoding='utf-8')
    base = ['-game', 'quake3', '-fs_basepath', directory, '-fs_homepath', directory/'home', '-threads', 1]
    run(compiler, [*base, '-meta', source], directory, 'carrier')
    original = source.with_suffix('.bsp').read_bytes()
    saved = {source.with_suffix('.bsp'): original, source.with_suffix('.srf'): source.with_suffix('.srf').read_bytes()}
    for ext in ('.prt', '.lin', '.reg', '.obj', '.mtl'):
        saved[source.with_suffix(ext)] = f'previous {ext} output\n'.encode()
    maps = {style: adapt(plain, style) for style in ('quake', 'bp', 'valve')}
    point_fields = [1, 2, 3, 6, 7, 8, 11, 12, 13]
    texture_fields = {'quake': list(range(16, 21)), 'bp': [17, 18, 19, 22, 23, 24],
                      'valve': [17, 18, 19, 20, 23, 24, 25, 26, 28, 29, 30]}
    valid, rejected, controls = [], [], []

    def accept(label, text, *, game='quake3', options=('-meta',), compare=True, expected=None):
        source.write_text(text, encoding='utf-8')
        command = ['-game', game, '-fs_basegame', 'baseq3', *base[2:], *options, source]
        run(compiler, command, directory, label)
        current = source.with_suffix('.bsp').read_bytes()
        lumps = payloads(current)
        if expected is not None: assert lumps == expected, (label, 'degenerate side changed geometry')
        record = {'case': label, 'lump_hashes': [digest(lump) for lump in lumps]}
        if reference and compare:
            run(reference, command, directory, label+'-reference')
            assert payloads(source.with_suffix('.bsp').read_bytes()) == lumps, (label, 'valid-source output changed')
            record['reference_lump_parity'] = True
        source.with_suffix('.bsp').write_bytes(current)
        valid.append(record)
        return lumps

    for style, text in maps.items():
        first = next(line for line in text.splitlines() if line.startswith('( ')).split()
        numeric = point_fields+texture_fields[style]
        variants = [('ordinary', text),
                    ('scientific', edit_side(text, {i: f'{float(first[i]):+.17e}' for i in numeric})),
                    ('quoted', edit_side(text, {i: '"'+first[i]+'"' for i in numeric})),
                    ('unsigned-flags', edit_side(text, {len(first)-3: '2147483648', len(first)-2: '4294967295', len(first)-1: '+4294967295'})),
                    ('signed-flags', edit_side(text, {len(first)-3: '-2147483648', len(first)-2: '-1', len(first)-1: '-2147483648'}))]
        # Plane points identify an infinite plane: tangential coordinates can be
        # outside the world even when the enclosed brush is entirely in bounds.
        far = dict(zip(point_fields, ['-272', '-1048576', '-1048576', '-272', '1048576', '-1048576', '-272', '-1048576', '1048576']))
        variants.append(('distant-plane-points', edit_side(text, far)))
        no_flags = '\n'.join(' '.join(line.split()[:-3]) if line.startswith('( ') else line for line in text.splitlines())+'\n'
        variants.append(('no-flags', no_flags))
        if style != 'bp':
            scales = [19, 20] if style == 'quake' else [29, 30]
            variants.extend([('zero-scales', edit_side(text, {i: '0' for i in scales})),
                             ('negative-scales', edit_side(text, {scales[0]: '-0.5', scales[1]: '-0.75'}))])
        else:
            variants.append(('zero-matrix', edit_side(text, {i: '0' for i in texture_fields[style]})))
        for game in ('quake3', 'ja'):
            for name, content in variants:
                for options in ((), ('-meta',)):
                    accept(game+'-'+style+'-'+name+('-meta' if options else '-ordinary'), content, game=game, options=options)
            expected = accept(game+'-'+style+'-degenerate-control', text, game=game)
            degenerate = dict(zip(point_fields, ['-272', '-272', '-16']*3))
            line = next(line for line in edit_side(text, degenerate).splitlines() if line.startswith('( '))
            original_line = next(line for line in text.splitlines() if line.startswith('( '))
            accept(game+'-'+style+'-degenerate', text.replace(original_line, line+'\n'+original_line, 1),
                   game=game, compare=False, expected=expected)
            assert b'degenerate plane' in (directory/(game+'-'+style+'-degenerate.log')).read_bytes()

    def reject(label, text, *, options=('-meta',), path=source):
        for destination, contents in saved.items(): destination.write_bytes(contents)
        data = text.encode()
        path.write_bytes(data)
        result = subprocess.run([str(compiler), *map(str, [*base, *options, path])], cwd=directory,
                                capture_output=True, timeout=20)
        output = result.stdout+result.stderr
        (directory/(label+'.log')).write_bytes(output)
        assert result.returncode == 1 and b'ERROR' in output, (label, result.returncode, output[-2500:])
        assert b'AddressSanitizer' not in output and b'runtime error:' not in output, label
        assert b'Allocation failed' not in output and b'Memory allocation failed' not in output, label
        assert path.read_bytes() == data
        for destination, contents in saved.items():
            if destination != path: assert destination.read_bytes() == contents, (label, destination, 'changed previous output')
        rejected.append(label)

    for style, text in maps.items():
        first = next(line for line in text.splitlines() if line.startswith('( ')).split()
        for field in point_fields:
            for value in ('nan', '7junk'):
                reject(f'{style}-point-{field}-{value}', edit_side(text, {field: value}))
        for field in texture_fields[style]:
            for value in ('nan', '7junk', '3.5e38', '1e-46'):
                reject(f'{style}-texture-{field}-{value}', edit_side(text, {field: value}))
        for field in range(len(first)-3, len(first)):
            for value in ('0junk', '4294967296', '-2147483649', '1.5', '1e0', '+-1'):
                reject(f'{style}-flag-{field}-{value}', edit_side(text, {field: value}))
        for value in ('inf', '-inf', '1e309', '1e-400', '0x10', '+-2', '""', '"2\0junk"'):
            reject(f'{style}-point-spelling-{len(rejected)}', edit_side(text, {1: value}))
        reject(style+'-plane-distance-overflow', edit_side(text, {i: '1e39' for i in (1, 6, 11)}))
        reject(style+'-plane-arithmetic-overflow', edit_side(text, {7: '1e100', 13: '1e100'}))
        reject(style+'-empty-material', edit_side(text, {27 if style == 'bp' else 15: '""'}))

    for style, field in [('quake', 19), ('valve', 29)]:
        reject(style+'-derived-axis-overflow', edit_side(maps[style], {field: '1e-40'}))
        reject(style+'-derived-uv-overflow', edit_side(maps[style], {field: '1e-37'}))
    reject('bp-derived-uv-overflow', edit_side(maps['bp'], {17: '1e38'}))
    reject('valve-derived-axis-overflow-large', edit_side(maps['valve'], {17: '3e38', 29: '0.01'}))

    bad = edit_side(maps['quake'], {1: '7junk'})
    for label, options in [('onlyents', ['-onlyents']), ('light', ['-light', '-fast']),
                           ('convert-map', ['-convert', '-readmap', '-format', 'map']),
                           ('convert-obj', ['-convert', '-readmap', '-format', 'obj']), ('nodetail', ['-nodetail'])]:
        reject(label, bad, options=options)
    reject('region-source', bad, path=source.with_suffix('.reg'))
    temporary = directory/'editor-temp.map'
    temporary.write_bytes(bad.encode())
    reject('editor-temp', plain, options=('-tempname', temporary, '-meta'))
    assert temporary.read_bytes() == bad.encode()

    # A redundant distant plane is allowed even above signed-int hash range;
    # only the plane's hash arithmetic is being exercised, not giant-world support.
    text = maps['quake']
    original_line = next(line for line in text.splitlines() if line.startswith('( '))
    for distance in ('2147483648', '1e10'):
        line = next(line for line in edit_side(text, {i: '-'+distance for i in (1, 6, 11)}).splitlines() if line.startswith('( '))
        for destination, contents in saved.items(): destination.write_bytes(contents)
        source.write_text(text.replace(original_line, original_line+'\n'+line, 1), encoding='utf-8')
        run(compiler, [*base, '-onlyents', source], directory, 'large-plane-hash-'+distance)
        assert payloads(source.with_suffix('.bsp').read_bytes())[1:] == payloads(original)[1:]
        for destination, contents in saved.items():
            if destination.suffix != '.bsp': assert destination.read_bytes() == contents
        controls.append('large-plane-hash-'+distance)

    report = {'compiler_sha256': digest(compiler.read_bytes()),
              'reference_sha256': digest(reference.read_bytes()) if reference else None,
              'valid_cases': valid, 'rejected_cases': rejected, 'controls': controls,
              'previous_outputs_preserved': True, 'degenerate_side_lump_parity': True}
    (root/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f'{len(valid)} valid native builds, {len(rejected)} rejected inputs/modes and {len(controls)} large-plane hash controls passed')


if __name__ == '__main__':
    main()

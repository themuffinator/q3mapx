"""Native MAP/BSP entity structure and bounded script include regressions.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import zipfile

from fixtures import create_fixture
from integration import run
from patch_input import payloads


def digest(data):
    return hashlib.sha256(data).hexdigest()


def with_entities(bsp, text):
    # Preserve every other native lump verbatim. The entity lump may be anywhere.
    result = bytearray(bsp)
    result.extend(b'\0' * (-len(result) % 4))
    struct.pack_into('<2i', result, 8, len(result), len(text))
    result.extend(text)
    return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--work-dir', required=True, type=Path)
    args = parser.parse_args()
    compiler, root = args.compiler.resolve(), args.work_dir.resolve()
    reference = args.reference.resolve() if args.reference else None
    directory = root/'native'
    source = create_fixture(directory, patch=False)
    plain = source.read_text(encoding='utf-8').encode()
    game_dir = source.parent.parent
    shader = game_dir/'scripts/q3mapx_tests.shader'
    original_shader = shader.read_bytes()
    base = ['-game', 'quake3', '-fs_basepath', directory, '-fs_homepath', directory/'home', '-threads', 1]
    run(compiler, [*base, '-meta', source], directory, 'carrier')
    original_bsp = source.with_suffix('.bsp').read_bytes()
    saved = {source.with_suffix('.bsp'): original_bsp,
             source.with_suffix('.srf'): source.with_suffix('.srf').read_bytes()}
    for extension in ('.prt', '.lin', '.reg', '.obj', '.mtl'):
        saved[source.with_suffix(extension)] = f'previous {extension} output\n'.encode()

    maps = {'map': plain}
    for format_name in ('map_bp', 'map_220'):
        run(compiler, [*base, '-convert', '-format', format_name, source.with_suffix('.bsp')], directory, format_name)
        converted = source.with_name('fixture_converted.map').read_text(encoding='utf-8')
        maps[format_name] = ('\n'.join(line.lstrip() for line in converted.splitlines())+'\n').encode()
    included = source.parent/'body.inc'
    included.write_bytes(plain)
    empty = source.parent/'empty.inc'
    empty.write_bytes(b'')
    (source.parent/'space name.inc').write_bytes(plain)
    chain = source.parent/'chain'
    chain.mkdir(exist_ok=True)
    for number in range(1, 64):
        (chain/f'{number}.inc').write_bytes(plain if number == 63 else
                                         f'$include maps/chain/{number+1}.inc\n'.encode())
    (source.parent/'cycle.inc').write_bytes(b'$include maps/./cycle.inc\n')
    # A single modest input is expanded repeatedly to exercise the cumulative
    # byte budget without an oversized source, allocation or archive expansion.
    (source.parent/'padding.inc').write_bytes(b'//' + b' ' * (1024*1024-3) + b'\n')
    archive = game_dir/'q3mapx_includes.pk3'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as pak:
        pak.writestr('maps/packed.inc', plain)
        pak.writestr('maps/packed-cycle.inc', '$include maps/packed-cycle.inc\n')
        pak.writestr('maps/packed-child.inc', '// child\n')
        pak.writestr('maps/packed-parent.inc', '$include maps/packed-child.inc\ngarbage\n')

    valid, rejected = [], []

    def accept(label, data, *, game='quake3', expected=None, compare=True):
        source.write_bytes(data)
        options = ['-game', game, '-fs_basegame', 'baseq3', *base[2:], '-meta', source]
        run(compiler, options, directory, label)
        bsp = source.with_suffix('.bsp').read_bytes()
        lumps = payloads(bsp)
        if expected is not None:
            assert lumps == expected, (label, 'include expansion changed output')
        record = {'case': label, 'lump_hashes': [digest(p) for p in lumps]}
        if reference and compare:
            run(reference, options, directory, label+'-reference')
            assert payloads(source.with_suffix('.bsp').read_bytes()) == lumps, (label, 'valid-source parity')
            record['reference_lump_parity'] = True
        valid.append(record)
        source.with_suffix('.bsp').write_bytes(bsp)
        return lumps

    for game in ('quake3', 'ja'):
        for format_name, content in maps.items():
            expected = accept(game+'-'+format_name, content, game=game)
            included.write_bytes(content)
            accept(game+'-'+format_name+'-include', b'$include maps/body.inc\n', game=game, expected=expected)
    included.write_bytes(plain)
    expected = accept('plain-control', plain)
    for label, content in (
            ('quoted-path', b'$include "maps/space name.inc"\n'),
            ('packed', b'$include maps/packed.inc\n'),
            ('depth-boundary', b'$include maps/chain/1.inc\n'),
            ('shallow-boundary', b'$include maps/empty.inc\n' * 1023 + plain),
            ('comment-boundary', b'$include /* comment */ maps/body.inc // EOF comment'),
            ('return-at-eof', b'$include maps/body.inc')):
        accept(label, content, expected=expected)
    # Includes may contain fragments; preserve ordinary textual composition.
    marker = b'"message" "q3mapx regression"\n'
    prefix, rest = plain.split(marker, 1)
    fragment, suffix = rest.split(b'\n}\n{\n"classname" "info_player_deathmatch"', 1)
    (source.parent/'geometry.inc').write_bytes(fragment+b'\n')
    accept('fragment', prefix+marker+b'$include maps/geometry.inc\n}\n{\n"classname" "info_player_deathmatch"'+suffix,
           expected=expected)
    (game_dir/'scripts/material.inc').write_bytes(original_shader)
    shader.write_bytes(b'$include scripts/material.inc\n')
    accept('shader-include', plain, expected=expected)
    shader.write_bytes(original_shader)
    # Literal directive text and quoted delimiters must survive both MAP loading
    # and the independent ParseFromMemory path when the BSP is decompiled.
    literals = b'"message" "$include"\n"{" "}"\n"}" "{"\n"$include" "literal"\n'
    for game in ('quake3', 'ja'):
        lumps = accept(game+'-literal-keys-values', plain.replace(marker, literals), game=game, compare=False)
        for pair in literals.splitlines(): assert pair in lumps[0], (game, pair)
        run(compiler, ['-game', game, '-fs_basegame', 'baseq3', *base[2:], '-convert', '-format', 'map',
                       source.with_suffix('.bsp')], directory, game+'-literal-decompile')
        recovered = source.with_name('fixture_converted.map').read_bytes()
        for pair in literals.splitlines(): assert pair in recovered, (game, pair)
    # Brush-primitive metadata, including braces, remains editor data.
    bp = maps['map_bp']
    assert b'brushDef\n{\n' in bp
    metadata = bp.replace(b'brushDef\n{\n', b'brushDef\n{\n"{" "}"\n"note" "$include"\n', 1)
    accept('brush-metadata', metadata, compare=False)

    def reject(label, data, *, options=('-meta',), path=source, message=None, timeout=15):
        for destination, contents in saved.items(): destination.write_bytes(contents)
        path.write_bytes(data)
        result = subprocess.run([str(compiler), *map(str, [*base, *options, path])], cwd=directory,
                                capture_output=True, timeout=timeout)
        output = result.stdout + result.stderr
        (directory/(label+'.log')).write_bytes(output)
        assert result.returncode == 1 and b'ERROR' in output, (label, result.returncode, output[-2000:])
        assert b'AddressSanitizer' not in output and b'runtime error:' not in output, label
        assert b'Allocation failed' not in output and b'Memory allocation failed' not in output, label
        if message: assert message in output, (label, message, output[-2000:])
        assert path.read_bytes() == data, (label, 'changed input')
        for destination, contents in saved.items():
            if destination != path:
                assert destination.read_bytes() == contents, (label, 'changed previous output', destination)
        rejected.append(label)
        return output

    malformed = {
        'empty': b'', 'whitespace': b' \t\n', 'comment-only': b'// no entities',
        'first-word': b'garbage\n', 'first-close': b'}\n', 'quoted-open': b'"{"\n',
        'unclosed-entity': prefix+marker, 'unclosed-primitive': prefix+marker+b'{',
        'missing-last-close': plain.rstrip()[:-1], 'trailing-word': plain+b'garbage\n',
        'missing-value-close': plain.replace(marker, b'"message" }\n'),
        'missing-value-open': plain.replace(marker, b'"message" {\n'),
        'missing-value-eof': prefix+b'"message"',
        'no-worldspawn': b'{\n"classname" "light"\n}\n',
        'group-first': b'{\n"classname" "func_group"\n}\n',
        'empty-world-entity': b'{\n}\n',
        'unsupported-terrain': prefix+marker+b'{\nterrainDef\n{\n}\n}\n}\n',
        'unknown-primitive': prefix+marker+b'{\nunknown\n}\n}\n',
        'quoted-primitive': prefix+marker+b'{\n"brushDef"\n}\n}\n',
        'missing-include': b'$include maps/missing.inc\n'+plain,
        'include-no-name': b'$include', 'include-empty-name': b'$include ""\n'+plain,
        'include-next-line': b'$include\nmaps/body.inc\n',
        'include-directive-name': b'$include $include maps/body.inc\n',
        'cycle': b'$include maps/cycle.inc\n',
        'packed-cycle': b'$include maps/packed-cycle.inc\n',
        'shallow-limit': b'$include maps/empty.inc\n' * 1024 + plain,
        'bp-metadata-eof': prefix+marker+b'{\nbrushDef\n{\n"key" "value"\n',
        'bp-metadata-value': prefix+marker+b'{\nbrushDef\n{\n"key"',
        'bp-metadata-brace': prefix+marker+b'{\nbrushDef\n{\n"key" }\n}\n}\n',
    }
    for label, content in malformed.items(): reject(label, content)
    (chain/'63.inc').write_bytes(b'$include maps/chain/64.inc\n')
    (chain/'64.inc').write_bytes(plain)
    reject('depth-limit', b'$include maps/chain/1.inc\n', message=b'Script include limit')
    output = reject('byte-limit', b'$include maps/padding.inc\n' * 256 + plain,
                    message=b'remaining script budget', timeout=90)
    assert b'line 256 in '+str(source).replace('\\', '/').encode() in output.replace(b'\\', b'/')
    # EOF after each token in the first brush side also covers same-line readers
    # which previously received a stale token after the script stack was popped.
    for format_name, content in maps.items():
        start = re.search(rb'\{\s*(?:brushDef\s*\{\s*)?\(', content).start()
        side_end = content.index(b'\n', content.index(b'( ', start))
        tokens = content[start:side_end].split()
        for count in range(1, len(tokens)+1):
            reject(f'{format_name}-truncated-{count}', content[:start]+b' '.join(tokens[:count]))
    # Parent locations must be restored when returning from a loose/packed child.
    output = reject('return-diagnostic', b'$include maps/empty.inc\ngarbage\n')
    assert b'line 2 in '+str(source).replace('\\', '/').encode() in output.replace(b'\\', b'/')
    output = reject('packed-return-diagnostic', b'$include maps/packed-parent.inc\n')
    assert b'line 2 in ' in output and b' :: maps/packed-parent.inc' in output
    shader.write_bytes(b'$include scripts/missing.inc\n')
    reject('shader-missing-include', plain, message=b'Cannot include')
    shader.write_bytes(original_shader)
    for label, options in [('onlyents', ['-onlyents']), ('light', ['-light', '-fast']),
                           ('convert-map', ['-convert', '-readmap', '-format', 'map']),
                           ('convert-obj', ['-convert', '-readmap', '-format', 'obj'])]:
        reject(label+'-empty', b'', options=options)
        reject(label+'-unclosed', plain.rstrip()[:-1], options=options)
    reject('region-source', plain.rstrip()[:-1], path=source.with_suffix('.reg'))
    temporary = directory/'editor-temp.map'
    temporary.write_bytes(plain.rstrip()[:-1])
    reject('editor-temp', plain, options=('-tempname', temporary, '-meta'))
    assert temporary.read_bytes() == plain.rstrip()[:-1]

    # A BSP must never expand include directives from its entity lump. A real
    # include file containing valid entity text makes an accidental load observable.
    entities = payloads(original_bsp)[0].rstrip(b'\0')
    (source.parent/'entities.inc').write_bytes(entities)
    bsp_path = directory/'entity-input.bsp'
    converted = bsp_path.with_name('entity-input_converted.map')
    sentinel = b'previous decompilation\n'
    cases = [('bsp-empty', b''),
             ('bsp-directive', b'$include maps/entities.inc\n\0'),
             ('bsp-open-key', entities.replace(b'"message" "q3mapx regression"', b'{ "x"')),
             ('bsp-missing-value', entities.replace(b'"message" "q3mapx regression"', b'"message" }')),
             ('bsp-missing-close', entities.rstrip()[:-1]),
             ('bsp-quoted-open', b'"{"\n"classname" "worldspawn"\n}\n\0')]
    for label, text in cases:
        converted.write_bytes(sentinel)
        reject(label, with_entities(original_bsp, text), path=bsp_path,
               options=('-convert', '-format', 'map'))
        assert converted.read_bytes() == sentinel
        output = (directory/(label+'.log')).read_bytes()
        assert b'entering maps/entities.inc' not in output

    report = {'compiler_sha256': digest(compiler.read_bytes()),
              'reference_sha256': digest(reference.read_bytes()) if reference else None,
              'valid_cases': valid, 'rejected_cases': rejected,
              'bsp_include_expansion_disabled': True,
              'previous_outputs_preserved': True, 'parent_locations_restored': True}
    (root/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f'{len(valid)} native valid-source cases and {len(rejected)} rejected inputs/modes passed')


if __name__ == '__main__':
    main()

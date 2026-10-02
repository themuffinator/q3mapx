"""Merged row ownership and padding on native carrier BSPs.
SPDX-License-Identifier: GPL-3.0-or-later.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from fixtures import create_fixture
from integration import Bsp, run
from vis_merge import portal, prt, rectangle, rows


def carrier(exe, root):
    source = create_fixture(root, patch=False)
    base = ['-game', 'quake3', '-fs_basepath', root, '-fs_homepath', root/'home']
    run(exe, [*base, '-threads', 1, '-meta', source], root, 'bsp')
    data = bytearray(Bsp(source.with_suffix('.bsp')).data)
    offset, length = struct.unpack_from('<ii', data, 8+4*8)
    for at in range(offset, offset+length, 48):
        if struct.unpack_from('<i', data, at)[0] >= 0:
            struct.pack_into('<i', data, at, 0)
    target = root/'rows.bsp'
    target.write_bytes(data)
    return base, target, Bsp(target)


def components(sizes):
    first, openings, expected = 0, [], []
    for size in sizes:
        expected.extend([((1 << size)-1) << first]*size)
        for i in range(size-1):
            openings.append(portal(first+i, first+i+1, rectangle(i*8)))
        first += size
    return prt(first, openings), expected


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--reference', type=Path)
    p.add_argument('--work-dir', type=Path, required=True)
    args = p.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    base, target, original = carrier(exe, root)
    records = []
    # These PRTs exercise assembly/ownership, not spatial matches to the carrier.
    for name, sizes in [('long-chain', [2048]), ('disconnected', [1,31,64,65,127,129,257])]:
        text, expected = components(sizes)
        reference = None
        for flags in ([], ['-fast'], ['-nopassage'], ['-passageOnly']):
            for workers in (1,4,20):
                label = f'{name}-{len(records)}'
                target.write_bytes(original.data)
                target.with_suffix('.prt').write_text(text)
                profile = root/(label+'.json')
                options = [*base, '-threads', workers, '-vis', '-reproducible', '-saveprt', '-merge', *flags, target]
                run(exe, ['-profile', profile, *options], root, label, timeout=180)
                output = Bsp(target)
                assert rows(output) == expected, (label, 'independent connected-component rows differ')
                if reference is None:
                    reference = output.lump(16)
                assert output.lump(16) == reference, (label, 'solver/worker bytes differ')
                for lump in range(1,16):
                    assert output.lump(lump) == original.lump(lump), (label, lump)
                assert target.with_suffix('.prt').read_text() == text
                log = (root/(label+'.log')).read_text()
                match = re.search(r'VIS row assembly: (\d+) distinct / (\d+) clusters', log)
                assert match and tuple(map(int,match.groups())) == (len(sizes),sum(sizes))
                total = re.search(r'Total visible clusters: (\d+)', log)
                assert total and int(total[1]) == sum(size*size for size in sizes)
                passes = json.loads(profile.read_text())['passes']
                assembly = [item for item in passes if item['name'] == 'AssembleVisRows']
                assert len(assembly) == 1 and assembly[0]['items'] == len(sizes)
                if args.reference and workers == 1 and not flags:
                    target.write_bytes(original.data)
                    run(args.reference.resolve(), options, root, label+'-reference', timeout=300)
                    assert Bsp(target).lump(16) == reference, (label, 'preceding implementation differs')
                records.append(dict(case=name, options=flags, workers=workers,
                                    clusters=sum(sizes), distinct_rows=len(sizes),
                                    visibility_sha256=hashlib.sha256(reference).hexdigest()))
    (root/'validation.json').write_text(json.dumps(dict(schema_version=1,
        compiler_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), cases=records, result='passed'), indent=2)+'\n')
    print('24 native merged-row controls passed: long chains, disconnected/isolated groups, all solvers and 1/4/20 workers')


if __name__ == '__main__':
    main()

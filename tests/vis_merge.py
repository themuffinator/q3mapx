"""Portal merge boundaries and compact VIS regression. SPDX-License-Identifier: GPL-3.0-or-later."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess

from fixtures import create_fixture
from integration import Bsp, run
from vis_fixtures import create_vis_fixture


def rectangle(x, y0=-64, y1=64, z0=-64, z1=64):
    # PRT winding: forward direction points along +X.
    return [(x, y0, z0), (x, y1, z0), (x, y1, z1), (x, y0, z1)]


def portal(a, b, points, flags=0):
    return f'{len(points)} {a} {b} {flags} ' + ' '.join('(%0.9g %0.9g %0.9g)' % p for p in points)


def prt(clusters, portals, faces=()):
    return '\n'.join(['PRT1', str(clusters), str(len(portals)), str(len(faces)), *portals, *faces]) + '\n'


def rows(bsp):
    data = bsp.lump(16)
    count, stride = struct.unpack_from('<ii', data)
    return [int.from_bytes(data[8 + i*stride:8 + (i+1)*stride], 'little') for i in range(count)]


def count(log, label):
    match = re.search(r'^\s*(\d+) ' + re.escape(label) + r'\s*$', log, re.M)
    assert match, (label, log)
    return int(match[1])


def passage_storage(log, options, live, bitset_bytes):
    match = re.search(r'Passage storage: (\d+) retained / (\d+) dense bytes; (\d+) empty masks; (\d+) blocks', log)
    if '-fast' in options or '-nopassage' in options:
        assert match is None
        return None
    assert match, 'Missing retained passage storage diagnostics'
    retained, dense, empty, blocks = map(int, match.groups())
    memory = re.search(r'(\d+) bytes required passage memory \((\d+) passages\)', log)
    candidates = re.search(r'Passage candidate tests: (\d+) / (\d+) dense portal visits', log)
    assert memory and candidates
    passages = int(memory[2]); tested, visits = map(int, candidates.groups())
    assert int(memory[1]) == retained and passages*8 <= retained <= dense
    assert 0 <= empty <= passages and 0 <= blocks <= live and blocks <= passages
    assert tested <= visits == passages*live
    assert retained <= passages*(8+bitset_bytes)
    return {'retained_bytes':retained, 'dense_bytes':dense, 'empty_masks':empty,
            'blocks':blocks, 'passages':passages, 'candidate_visits':tested,'dense_visits':visits}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    parser.add_argument('--reference', type=Path, help='Optional uncompressed solver for exact byte comparison')
    parser.add_argument('--grid', type=int, default=5)
    parser.add_argument('--workers', type=int, nargs='+', default=[1, 4, 20])
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    source = create_fixture(root, patch=False)
    source.write_text(source.read_text().replace('"classname" "worldspawn"',
                      '"classname" "worldspawn"\n"_farplanedist" "64o"'))
    base = ['-game', 'quake3', '-fs_basepath', root]
    run(exe, [*base, '-meta', source], root, 'bsp')
    original = Bsp(source.with_suffix('.bsp'))
    # Use real validated BSP storage with a synthetic PRT graph to isolate VIS
    # contracts. These are graph fixtures, not claimed spatial BSP/PRT matches.
    data = bytearray(original.data)
    offset, length = original.lumps[4]
    for at in range(offset, offset+length, 48):
        if struct.unpack_from('<i', data, at)[0] >= 0:
            struct.pack_into('<i', data, at, 0)
    original_bytes = bytes(data)
    target = source.with_name('graph.bsp')
    records, map_records = [], []

    def solve(name, text, options, workers=1):
        target.write_bytes(original_bytes)
        target.with_suffix('.prt').write_text(text)
        (root/(name+'.prt')).write_text(text)
        result = run(exe, [*base, '-threads', workers, '-vis', '-reproducible', '-saveprt', *options, target],
                     root, name, timeout=90)
        bsp = Bsp(target)
        # VIS must retain native cluster IDs and all non-entity/non-VIS lumps.
        for lump in range(1, 16):
            before = BspBytes(original_bytes, original.lumps).lump(lump)
            assert bsp.lump(lump) == before, (name, lump)
        log = (root / f'{name}.log').read_text()
        matrix = rows(bsp)
        assert all(row & (1 << i) for i, row in enumerate(matrix)), name
        assert all(row < (1 << len(matrix)) for row in matrix), (name, 'padding bits')
        total = re.search(r'Total visible clusters: (\d+)', log)
        assert total and int(total[1]) == sum(row.bit_count() for row in matrix), name
        record = {'case': name, 'workers': workers, 'options': options, 'seconds': result['seconds'],
                  'prt_sha256_lf': hashlib.sha256(text.encode()).hexdigest(),
                  'active_directions': count(log, 'active portals'),
                  'visibility_sha256': hashlib.sha256(bsp.lump(16)).hexdigest()}
        compact = re.search(r'VIS portal bitsets: (\d+) live / (\d+) input directions; (\d+) -> (\d+) bytes each', log)
        if compact:
            record['bitsets'] = dict(zip(('live', 'input', 'original_bytes', 'bytes'), map(int, compact.groups())))
            assert record['bitsets']['live'] == record['active_directions']
            assert record['bitsets']['bytes'] == ((record['active_directions'] + 63) // 64) * 8
            record['passage_storage'] = passage_storage(log, options, record['active_directions'], record['bitsets']['bytes'])
        if args.reference:
            expected = bsp.lump(16)
            target.write_bytes(original_bytes)
            run(args.reference.resolve(), [*base, '-threads', workers, '-vis', '-reproducible', '-saveprt', *options, target],
                root, name+'-reference', timeout=90)
            assert Bsp(target).lump(16) == expected, (name, 'reference VIS differs')
            record['reference_byte_parity'] = True
        records.append(record)
        return matrix, log

    chain = [portal(i, i+1, rectangle(128*i), flags=2) for i in range(4)]
    # Sky suppresses far-plane distance culling in both directions. Ordinary
    # portals remain distance-limited, so this also checks that the key is active.
    for solver in ([], ['-fast'], ['-nopassage'], ['-passageOnly']):
        matrix, _ = solve('sky-'+str(len(records)), prt(5, chain), solver)
        assert matrix == [31]*5, matrix
        plain = [line.replace(' 2 (', ' 0 (', 1) for line in chain]
        matrix, _ = solve('distance-'+str(len(records)), prt(5, plain), solver)
        assert matrix != [31]*5 and matrix[0] == 3 and matrix[4] == 24, matrix

    # Hint protection must hold from both input orientations, and cannot be
    # bypassed using an unhinted second opening between the same two leaves.
    for reverse in (False, True):
        openings = [portal(i+1, i, list(reversed(rectangle(i))), int(i == 4)) if reverse
                    else portal(i, i+1, rectangle(i), int(i == 4)) for i in range(9)]
        for mode in ('-merge', '-hint'):
            _, log = solve(f'hint-{reverse}-{mode}', prt(10, openings), [mode, '-fast'])
            assert count(log, 'leaves merged') == 8, log
            assert count(log, 'active portals') == count(log, 'hint portals') == 2, log
        openings = [portal(0, 1, rectangle(0, -64, 0)), portal(0, 1, rectangle(0, 0, 64), 1)]
        if reverse:
            openings = [portal(1, 0, list(reversed(rectangle(0, -64, 0)))),
                        portal(1, 0, list(reversed(rectangle(0, 0, 64))), 1)]
        _, log = solve(f'hint-parallel-{reverse}', prt(2, openings), ['-merge', '-fast'])
        assert count(log, 'leaves merged') == 0 and count(log, 'active portals') == 4, log

    # Complete contraction, including the zero-live-bitset path in all solvers.
    for solver in ([], ['-fast'], ['-nopassage'], ['-passageOnly']):
        matrix, log = solve('all-merged-'+str(len(records)), prt(2, [portal(0, 1, rectangle(0))]), ['-merge', *solver])
        assert matrix == [3, 3] and count(log, 'active portals') == 0

    for flags, label in ((0, 'coplanar'), (1, 'mixed-hint'), (2, 'mixed-sky')):
        openings = [portal(0, 1, rectangle(0, -64, 0)), portal(0, 1, rectangle(0, 0, 64), flags)]
        _, log = solve(label, prt(2, openings), ['-mergeportals', '-fast'])
        assert count(log, 'active portals') == (2 if flags == 0 else 4), log
    folded = [(0, 0, -64), (64, 64, -64), (64, 64, 64), (0, 0, 64)]
    _, log = solve('noncoplanar', prt(2, [portal(0, 1, rectangle(0, -64, 0)), portal(0, 1, folded)]), ['-mergeportals', '-fast'])
    assert count(log, 'active portals') == 4, log

    concave = [portal(0, 1, [(0, 0, 0), (0, 2, 0), (0, .5, .5)]),
               portal(0, 1, [(0, 0, 0), (0, .5, .5), (0, 0, 2)])]
    _, log = solve('concave-single-end', prt(2, concave), ['-mergeportals', '-fast'])
    assert count(log, 'active portals') == 4, log
    hourglass = [portal(0, 1, [(0, -2, -2), (0, 0, -.5), (0, 0, .5), (0, -2, 2)]),
                 portal(0, 1, [(0, 0, -.5), (0, 2, -2), (0, 2, 2), (0, 0, .5)])]
    _, log = solve('concave-union', prt(2, hourglass), ['-mergeportals', '-fast'])
    assert count(log, 'active portals') == 4, log

    # A union may have fewer points than the sum of its inputs. Test both sides
    # of the 512-point output cap without invoking separator-cache limits.
    for points in (257, 258):
        halves = []
        for start in (math.pi/2, 3*math.pi/2):
            polygon = [(0, 1024*math.cos(start+math.pi*i/(points-1)), 1024*math.sin(start+math.pi*i/(points-1)))
                       for i in range(points)]
            halves.append(portal(0, 1, polygon))
        _, log = solve(f'winding-{points}', prt(2, halves), ['-mergeportals', '-fast'])
        assert count(log, 'active portals') == (2 if points == 257 else 4), log

    left = rectangle(0, -64, 0) + [(0, -64, 64-128*i/255) for i in range(1, 255)]
    right = [(0, 64, 64), (0, 0, 64), (0, 0, -64), (0, 64, -64)]
    right += [(0, 64, -64+128*i/255) for i in range(1, 255)]
    assert len(left) == len(right) == 258
    _, log = solve('collinear-input-cap', prt(2, [portal(0, 1, left), portal(0, 1, right)]), ['-mergeportals', '-fast'])
    assert count(log, 'active portals') == 2, log

    # Each original leaf fits the 1024-entry limit; their proposed union may not.
    # Duplicated boundary faces isolate the storage contract from convexity.
    face = '4 {leaf} ' + ' '.join('(%g %g %g)' % p for p in rectangle(0))
    for extra in (0, 1):
        faces = [face.format(leaf=0)]*512 + [face.format(leaf=1)]*(512+extra)
        _, log = solve(f'face-cap-{extra}', prt(2, [portal(0, 1, rectangle(0))], faces), ['-merge', '-fast'])
        assert count(log, 'leaves merged') == (0 if extra else 1), log
        openings = [portal(0, 1, rectangle(0))]
        openings += [portal(0, 2, list(reversed(rectangle(-1))), 1)]*512
        openings += [portal(1, 3, rectangle(1), 1)]*(512+extra)
        _, log = solve(f'portal-cap-{extra}', prt(4, openings), ['-merge', '-fast'])
        assert count(log, 'leaves merged') == (0 if extra else 1), log

    # Malformed self-edges used to enter a merge cycle. Fail before publication.
    target.write_bytes(original_bytes)
    target.with_suffix('.prt').write_text(prt(2, [portal(0, 0, rectangle(0))]))
    for force in ([], ['-force']):
        result = subprocess.run([str(exe), *map(str, base), *force, '-vis', '-merge', str(target)],
                                cwd=root, capture_output=True, timeout=15)
        assert result.returncode == 1 and b'connects a leaf to itself' in result.stdout, result.stdout[-2000:]
        assert target.read_bytes() == original_bytes
        (root/f'self-edge-{bool(force)}.log').write_bytes(result.stdout+result.stderr)

    # The PRT is the retry input. It must survive a failed BSP publication even
    # without -saveprt, and be consumed only after successful publication.
    retry_prt = prt(2, [portal(0, 1, rectangle(0))])
    target.with_suffix('.prt').write_text(retry_prt)
    staging_pattern = target.name + '.q3mapx-*.tmp'
    staging_before = set(target.parent.glob(staging_pattern))
    failure_options = {}
    if os.name == 'nt':
        import ctypes
        from ctypes import wintypes
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p,
                                      wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
        kernel.CreateFileW.restype = wintypes.HANDLE
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        kernel.CloseHandle.restype = wintypes.BOOL
        handle = kernel.CreateFileW(str(target), 0x80000000, 1 | 2, None, 3, 0, None)
        assert handle not in (None, ctypes.c_void_p(-1).value), ctypes.get_last_error()
    else:
        import resource
        import signal
        def bounded_output():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            _, hard = resource.getrlimit(resource.RLIMIT_FSIZE)
            resource.setrlimit(resource.RLIMIT_FSIZE, (1024, hard))
        failure_options['preexec_fn'] = bounded_output
    try:
        result = subprocess.run([str(exe), *map(str, base), '-vis', '-fast', str(target)],
                                cwd=root, capture_output=True, timeout=30, **failure_options)
        (root/'publish-failure.log').write_bytes(result.stdout+result.stderr)
        assert result.returncode == 1 and b'ERROR' in result.stdout, result.stdout[-2000:]
        assert target.read_bytes() == original_bytes
        assert target.with_suffix('.prt').read_text() == retry_prt
        assert set(target.parent.glob(staging_pattern)) == staging_before, 'Failed BSP write left staged files'
        assert b'Wrote ' not in result.stdout, 'Failed publication reported success'
    finally:
        if os.name == 'nt':
            assert kernel.CloseHandle(handle)
    run(exe, [*base, '-vis', '-fast', target], root, 'publish-retry')
    assert not target.with_suffix('.prt').exists()
    for detail in (False, True):
        directory = root / ('manual-detail' if detail else 'poor-detail')
        source = create_vis_fixture(directory, grid=args.grid, detail=detail)
        base = ['-game', 'quake3', '-fs_basepath', directory]
        run(exe, [*base, '-meta', source], directory, 'bsp')
        original = Bsp(source.with_suffix('.bsp'))
        target = source.with_suffix('.bsp')
        controls = {}
        for merge in ([], ['-merge'], ['-mergeportals'], ['-hint'], ['-merge', '-nosort']):
            for solver in ([], ['-nopassage'], ['-passageOnly'], ['-fast']):
                expected = None
                for workers in args.workers:
                    target.write_bytes(original.data)
                    options = [*merge, *solver]
                    label = 'vis-'+str(len(map_records))
                    result = run(exe, [*base, '-threads', workers, '-vis', '-reproducible', '-saveprt', *options, source], directory, label)
                    output = Bsp(target)
                    for lump in range(1, 16):
                        assert output.lump(lump) == original.lump(lump), (detail, options, lump)
                    visibility = output.lump(16)
                    if expected is None:
                        expected = visibility
                    assert visibility == expected, (detail, options, workers, 'worker-dependent VIS')
                    log = (directory/f'{label}.log').read_text()
                    packed = re.search(r'VIS portal bitsets: (\d+) live / (\d+) input directions; (\d+) -> (\d+) bytes each', log)
                    assert packed, label
                    live, inputs, before, after = map(int, packed.groups())
                    assert live == count(log, 'active portals') and after == ((live+63)//64)*8
                    assert before == ((inputs+63)//64)*8
                    matrix = rows(output)
                    key = tuple(solver)
                    if not merge:
                        controls[key] = matrix
                    control = controls[key]
                    record = {'detail': detail, 'options': options, 'workers': workers,
                              'seconds': result['seconds'],
                              'input_directions': inputs, 'live_directions': live,
                              'bitset_bytes_before': before, 'bitset_bytes_after': after,
                              'visibility_sha256': hashlib.sha256(visibility).hexdigest(),
                              'visible_pairs': sum(row.bit_count() for row in matrix),
                              'added_baseline_bits': sum((a & ~b).bit_count() for a, b in zip(matrix, control)),
                              'missing_baseline_bits': sum((b & ~a).bit_count() for a, b in zip(matrix, control))}
                    record['passage_storage'] = passage_storage(log, options, live, after)
                    if args.reference:
                        target.write_bytes(original.data)
                        run(args.reference.resolve(), [*base, '-threads', workers, '-vis', '-reproducible', '-saveprt', *options, source],
                            directory, label+'-reference')
                        assert Bsp(target).lump(16) == visibility, (detail, options, workers, 'reference VIS differs')
                        record['reference_byte_parity'] = True
                    map_records.append(record)
    report = {'graph_checks': records, 'map_checks': map_records,
              'publication_failure_preserves_bsp_and_prt': True, 'successful_retry_consumes_prt': True,
              'publication_failure_cleans_staging': True}
    (root/'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'{len(records)} VIS graph checks, two controlled self-edge rejections and {len(map_records)} real-map worker checks passed')


class BspBytes:
    def __init__(self, data, lumps):
        self.data, self.lumps = data, lumps

    def lump(self, number):
        offset, size = self.lumps[number]
        return self.data[offset:offset+size]


if __name__ == '__main__':
    main()

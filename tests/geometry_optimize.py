"""Native geometry publication, material guards and independent preservation checks.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zipfile

from integration import Bsp, run
from planar_reduction import mesh_boundary, next_float
from planar_reduction_native import fixture, meshes


def sha(data):
    return hashlib.sha256(data).hexdigest()


def prepare(exe, directory, lighting):
    source = fixture(directory, lighting)
    shader = directory/'baseq3/scripts/q3mapx_tests.shader'
    text = shader.read_text(encoding='utf-8').replace('textures/q3mapx/grid\n{', 'textures/q3mapx/grid\n{\n    surfaceparm nomarks\n    surfaceparm nodlight')
    shader.write_text(text, encoding='utf-8')
    base = ['-game', 'quake3', '-fs_basepath', directory, '-fs_homepath', directory/'home', '-threads', '1']
    for stage, options in [('bsp', ['-meta']), ('vis', ['-vis']), ('light', ['-light', '-fast'])]:
        run(exe, [*base, *options, source], directory, stage)
    return source.with_suffix('.bsp'), shader


def unchanged_native(original, output, report):
    a, b = Bsp(original), Bsp(output)
    assert len(a.data) == len(b.data) and a.lumps == b.lumps
    mutable = set()
    changed = 0
    before_meshes = {m['surface']: m for m in meshes(a)}
    after_meshes = {m['surface']: m for m in meshes(b)}
    for surface in report['surfaces']:
        index = surface['surface']
        if surface['triangles_before'] == surface['triangles_after']:
            assert a.lump(13)[index*104:(index+1)*104] == b.lump(13)[index*104:(index+1)*104]
            if index in before_meshes: assert before_meshes[index]['triangles'] == after_meshes[index]['triangles']
            continue
        changed += 1
        assert surface['status'] == 'reduced' and not surface['reason']
        offset = a.lumps[13][0]+index*104
        first = struct.unpack_from('<i', b.data, offset+20)[0]
        count = surface['triangles_after']*3
        mutable.update(range(offset+20, offset+28))
        mutable.update(range(a.lumps[11][0]+first*4, a.lumps[11][0]+(first+count)*4))
        old, new = before_meshes[index], after_meshes[index]
        assert old['vertices'] == new['vertices']
        assert mesh_boundary(old['triangles']) == mesh_boundary(new['triangles'])
        assert len(old['triangles']) == len(new['triangles'])+surface['removed_vertices']*2
    assert changed == report['changed_surfaces']
    assert all(x == y or i in mutable for i, (x, y) in enumerate(zip(a.data, b.data)))
    assert sha(a.data) == report['source_sha256'] and sha(b.data) == report['result_sha256']
    return changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--work-dir', type=Path, required=True)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=True)
    records = []
    for lighting in ('ambient', 'point'):
        directory = root/lighting
        source, shader = prepare(exe, directory, lighting)
        base = ['-game', 'quake3', '-fs_basepath', directory, '-fs_homepath', directory/'home', '-renderer', 'quake3e-gl']
        original = source.read_bytes()
        outputs = []
        reports = []
        for workers in (1, 4):
            output, report_path = directory/f'optimized-{workers}.bsp', directory/f'optimized-{workers}.json'
            run(exe, [*base, '-threads', workers, '-optimize-geometry', '-o', output, '-report', report_path, source], directory, f'optimize-{workers}')
            report = json.loads(report_path.read_text())
            unchanged_native(source, output, report)
            outputs.append(output.read_bytes()); reports.append(report)
        assert outputs[0] == outputs[1] and reports[0] == reports[1]
        report = reports[0]
        grid = [s for s in report['surfaces'] if s['material'] == 'textures/q3mapx/grid']
        assert sum(s['triangles_before'] for s in grid) == 512
        after = sum(s['triangles_after'] for s in grid)
        if lighting == 'ambient':
            assert after == 144 and report['changed_surfaces'] == 6
        else:
            assert after == 512 and all(s['reason'] == 'quantized_vertex_color' for s in grid)
            assert outputs[0] == original
        run(exe, [*base[:-2], '-threads', '1', '-optimize-geometry', '-report', directory/'analysis.json', source], directory, 'analyze')
        proposal = json.loads((directory/'analysis.json').read_text())
        proposal['mode'] = 'write'
        assert proposal == report and source.read_bytes() == original
        run(exe, [*base, '-optimize-geometry', '-o', directory/'again.bsp', '-report', directory/'again.json', directory/'optimized-1.bsp'], directory, 'idempotence')
        assert (directory/'again.bsp').read_bytes() == outputs[0]
        run(exe, [*base[:-2], '-info', directory/'optimized-1.bsp'], directory, 'validate-output')
        records.append({'lighting': lighting, 'input_sha256': sha(original), 'output_sha256': sha(outputs[0]),
                        'grid_before': 512, 'grid_after': after, 'changed_surfaces': report['changed_surfaces']})
        if lighting != 'ambient':
            continue
        output, destination = directory/'guarded.bsp', directory/'guarded.json'
        successful_shader = shader.read_bytes()
        source_bsp = Bsp(source)
        grid_indices = [s['surface'] for s in grid]
        protected = []

        def optimize_guard(label, reason, target=None, options=()):
            selected = target or source
            run(exe, [*base, '-threads', '4', '-optimize-geometry', '-o', output, '-report', destination, *options, selected], directory, label)
            observed = json.loads(destination.read_text())
            unchanged_native(selected, output, observed)
            assert any(s['reason'] == reason for s in observed['surfaces']), (label, Counter(s['reason'] for s in observed['surfaces']))
            protected.append(label)
            return observed

        for label, insertion, reason in [
            ('deform', 'deformVertexes wave 1 sin 0 1 0 1', 'unsupported_material_directive:deformvertexes'),
            ('transparent', 'sort additive', 'nonopaque_sort'),
            ('offset', 'polygonOffset', 'unsupported_material_directive:polygonoffset'),
        ]:
            shader.write_bytes(successful_shader.replace(b'textures/q3mapx/grid\n{', b'textures/q3mapx/grid\n{\n'+insertion.encode()).replace(b'textures/q3mapx/grid\r\n{', b'textures/q3mapx/grid\r\n{\r\n'+insertion.encode()))
            observed = optimize_guard(label, reason)
            assert observed['changed_surfaces'] == 0 and output.read_bytes() == original
            shader.write_bytes(successful_shader)
        shader.write_bytes(successful_shader.replace(b'    surfaceparm nomarks', b'    surfaceparm nodlight'))
        assert optimize_guard('marks', 'mark_fragment_topology')['changed_surfaces'] == 0
        shader.write_bytes(successful_shader)
        shader.write_bytes(successful_shader.replace(b'    surfaceparm nodlight', b''))
        assert optimize_guard('dynamic-light', 'dynamic_light_rasterization')['changed_surfaces'] == 0
        shader.write_bytes(successful_shader)
        unlisted = shader.with_name('not_in_shaderlist.shader')
        unlisted.write_text('textures/q3mapx/grid { { map bad } }\n', encoding='utf-8')
        assert optimize_guard('duplicate', 'duplicate_material_definition')['changed_surfaces'] == 0
        unlisted.unlink()
        optimize_guard('exclude-material', 'user_excluded', options=['-exclude-shader', 'TEXTURES/Q3MAPX/GRID'])
        excluded = optimize_guard('exclude-surface', 'user_excluded', options=['-exclude-surface', str(grid_indices[0])])
        assert excluded['changed_surfaces'] == 5
        # Native guards use valid reference ranges. The untouched prefix/tails,
        # every other lump and complete source stay intact through each rewrite.
        def mutation(label, edit, reason):
            data = bytearray(original); edit(data)
            mutated = directory/f'{label}.bsp'; mutated.write_bytes(data)
            return optimize_guard(label, reason, mutated)

        first_surface = source_bsp.lumps[13][0]+grid_indices[0]*104
        first_vertex = struct.unpack_from('<i', original, first_surface+12)[0]
        v = source_bsp.lumps[10][0]+first_vertex*44
        mutation('height', lambda data: struct.pack_into('<f', data, v+8, 129), 'non_horizontal_geometry')
        mutation('color', lambda data: data.__setitem__(v+40, (data[v+40]+1)%256), 'quantized_vertex_color')
        shader_id = struct.unpack_from('<i', original, first_surface)[0]
        flag_offset = source_bsp.lumps[1][0]+shader_id*72+64
        mutation('native-dynamic-light', lambda data: struct.pack_into('<i', data, flag_offset,
            struct.unpack_from('<i', data, flag_offset)[0] & ~0x20000), 'dynamic_light_rasterization')
        mutation('native-marks', lambda data: struct.pack_into('<i', data, flag_offset,
            struct.unpack_from('<i', data, flag_offset)[0] & ~0x20), 'mark_fragment_topology')
        mutation('uv-seam', lambda data: struct.pack_into('<f', data, v+12, next_float(struct.unpack_from('<f', data, v+12)[0])), 'non_affine_surface_mapping')
        model = source_bsp.lumps[7][0]+40
        mutation('shared-owner', lambda data: struct.pack_into('<ii', data, model+24, min(grid_indices), max(grid_indices)-min(grid_indices)+1), 'non_world_or_shared_owner')
        second_surface = source_bsp.lumps[13][0]+grid_indices[1]*104
        shared_data=bytearray(original)
        struct.pack_into('<i', shared_data, second_surface+20, struct.unpack_from('<i', original, first_surface+20)[0])
        shared_source=directory/'shared-indices.bsp'; shared_source.write_bytes(shared_data)
        optimize_guard('shared-indices', 'user_excluded', shared_source, ['-exclude-surface', str(grid_indices[1])])
        failures = []

        def fail(label, options=(), target=source, report_path=destination, renderer=True, message=None):
            output.write_bytes(b'preserved BSP sentinel')
            if not report_path.is_dir(): report_path.write_bytes(b'preserved report sentinel')
            before = source.read_bytes()
            result = subprocess.run([str(exe), *map(str, base if renderer else base[:-2]), '-threads', '1', '-optimize-geometry', '-o', str(output), '-report', str(report_path), *map(str, options), str(target)], cwd=directory, capture_output=True, timeout=45)
            (directory/f'{label}.log').write_bytes(result.stdout+result.stderr)
            assert result.returncode != 0, label
            if message: assert message.encode() in result.stdout+result.stderr, label
            assert output.read_bytes() == b'preserved BSP sentinel' and source.read_bytes() == before, label
            if not report_path.is_dir(): assert report_path.read_bytes() == b'preserved report sentinel'
            assert not list(directory.glob('*.q3mapx-*')), label
            failures.append(label)

        fail('work-limit', ['-max-work', '1'])
        fail('unspecified-renderer', renderer=False)
        fail('unsupported-renderer', ['-renderer', 'original-quake3'], renderer=False)
        occupied = bytearray(original)
        struct.pack_into('<ii', occupied, first_surface+20, 0, len(source_bsp.lump(11))//4)
        occupied_path = directory/'occupied-indices.bsp'; occupied_path.write_bytes(occupied)
        fail('no-index-storage', ['-exclude-surface', str(grid_indices[0])], target=occupied_path,
             message='Insufficient contiguous unreferenced index storage')
        fail('bad-work', ['-max-work', 'invalid'])
        fail('bad-exclusion', ['-exclude-surface', '999999'])
        extended = directory/'extended.bsp'; extended.write_bytes(original+b'BSPX')
        fail('unknown-extension', target=extended)
        malformed = directory/'malformed.bsp'; malformed.write_bytes(original[:-4])
        fail('truncated', target=malformed)
        blocked = directory/'blocked.json'; blocked.mkdir(exist_ok=True)
        fail('report-directory', report_path=blocked)
        unlisted.write_bytes(b' '*(4*1024*1024+1))
        fail('oversized-loose-script')
        unlisted.unlink()
        archive = directory/'baseq3/oversized.pk3'
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as package:
            package.writestr('scripts/oversized.shader', b' '*(4*1024*1024+1))
        fail('oversized-packed-script')
        archive.unlink()
        result = subprocess.run([str(exe), *map(str, base), '-optimize-geometry', '-o', str(source), str(source)], cwd=directory, capture_output=True, timeout=20)
        assert result.returncode != 0 and source.read_bytes() == original
        records[-1]['protected_controls'] = protected
        records[-1]['preserved_failures'] = failures+['source-alias']
    (root/'validation.json').write_text(json.dumps({'compiler_sha256': sha(exe.read_bytes()), 'records': records}, indent=2)+'\n', encoding='utf-8')
    print('Native index-only reduction, full-byte preservation, 1/4-worker parity, material/native guards and atomic failure controls passed')


if __name__ == '__main__':
    main()

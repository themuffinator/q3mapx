"""Native meta ordering under irrelevant shader allocations and worker changes.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from fixtures import create_fixture, create_lighting_fixture
from integration import Bsp, run
from lightgrid_cli import lumps
from planar_reduction_native import fixture as grid_fixture


def digest(data):
    return hashlib.sha256(data).hexdigest()


def payloads(bsp):
    # Exclude unused file gaps (including the writer timestamp), not lump data.
    return [digest(bsp.lump(i)) for i in range(17)]


def semantic(bsp, *, initialize_unused_uvs=False):
    """Preserve surface contents, model ownership and every leaf association.

    Normalize global surface/vertex/index allocation. Optional reference mode
    also initializes unassigned pre-LIGHT UVs; local vertex/index order, all other
    surface attributes and every other lump remain part of the check.
    """
    vertices = bytearray(bsp.lump(10))
    if initialize_unused_uvs:
        # Compatibility with the preceding parser: only unassigned pre-LIGHT
        # coordinates may change. No geometry/texture/color/normal is masked.
        assert not bsp.lump(14)
        assert all(struct.unpack_from('<i', bsp.lump(13), i+28)[0] < 0
                   for i in range(0, len(bsp.lump(13)), 104))
        for i in range(0, len(vertices), 44):
            vertices[i+20:i+28] = bytes(8)
    surfaces = []
    for offset in range(0, len(bsp.lump(13)), 104):
        surface = bsp.lump(13)[offset:offset+104]
        shader, _, _, first_v, count_v, first_i, count_i = struct.unpack_from('<7i', surface)
        parts = [bsp.lump(1)[shader*72:(shader+1)*72], surface[4:12], surface[28:],
                 vertices[first_v*44:(first_v+count_v)*44],
                 bsp.lump(11)[first_i*4:(first_i+count_i)*4]]
        surfaces.append(digest(b''.join(struct.pack('<I',len(part))+part for part in parts)))
    models = []
    for offset in range(0, len(bsp.lump(7)), 40):
        first, count = struct.unpack_from('<2i', bsp.lump(7), offset+24)
        models.append(sorted(surfaces[first:first+count]))
    leaf_indices = struct.unpack('<'+'i'*(len(bsp.lump(5))//4), bsp.lump(5))
    leaves = []
    for offset in range(0, len(bsp.lump(4)), 48):
        first, count = struct.unpack_from('<2i', bsp.lump(4), offset+32)
        leaves.append(sorted(surfaces[index] for index in leaf_indices[first:first+count]))
    return {'surfaces': sorted(surfaces), 'models': models, 'leaves': leaves,
            'unchanged_lumps': {i:digest(bsp.lump(i)) for i in range(17) if i not in (5,10,11,13)}}


def padding(count):
    return ''.join(f'textures/unused/pad{i}\n{{\n {{ map textures/q3mapx/checker.tga }}\n}}\n'
                   for i in range(count))


def unassigned_uv_vertices(vertices, stride=44, slots=1):
    return sum(vertices[i+20:i+20+slots*8] != bytes(slots*8)
               for i in range(0, len(vertices), stride))


def raven_patches(compiler, root):
    """The source parser must initialize all four RBSP lightmap channels."""
    directory = root/'raven-patch'
    source = create_fixture(directory)
    shader = directory/'baseq3/scripts/q3mapx_tests.shader'
    original = shader.read_text(encoding='utf-8')
    cases = []
    try:
        for mode, options in [('ordinary', []), ('meta', ['-meta','-patchmeta'])]:
            expected = None
            for count in (0,13,255):
                shader.write_text(padding(count)+original, encoding='utf-8')
                for workers in (1,4):
                    name = f'{mode}-{count}-{workers}'
                    base = ['-game','ja','-fs_basepath',directory,'-fs_basegame','baseq3',
                            '-fs_homepath',directory/'home','-threads',workers]
                    run(compiler,[*base,*options,source],directory,name+'-bsp')
                    data = source.with_suffix('.bsp').read_bytes()
                    assert data[:8] == b'RBSP\x01\0\0\0'
                    payload = lumps(data)
                    assert len(payload) == 18 and not payload[14]
                    # T-junction repair deliberately uses channel four as a
                    # temporary edge ID on brush faces. Native patch control
                    # points must have all four channels initialized to zero.
                    assert not unassigned_uv_vertices(payload[10], 80, 3), name
                    patches = 0
                    for offset in range(0, len(payload[13]), 148):
                        kind, first, count_v = struct.unpack_from('<3i', payload[13], offset+8)
                        if kind == 2:
                            patches += 1
                            assert not unassigned_uv_vertices(payload[10][first*80:(first+count_v)*80],80,4), name
                    assert patches > 0
                    hashes = [digest(part) for part in payload]
                    if expected is None: expected = hashes
                    assert hashes == expected, (name, 'RBSP patch payloads changed')
                    cases.append({'mode':mode,'padding':count,'workers':workers,'bsp_lumps':hashes})
    finally:
        shader.write_text(original,encoding='utf-8')
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--reference', type=Path, help='Optional preceding compiler for semantic compatibility and fault reproduction')
    parser.add_argument('--work-dir', type=Path, required=True)
    args = parser.parse_args()
    compiler, root = args.compiler.resolve(), args.work_dir.resolve()
    reference = args.reference.resolve() if args.reference else None
    root.mkdir(parents=True, exist_ok=True)
    records = []
    for kind in ('model-grid', 'lighting-patch'):
        directory = root/kind
        source = grid_fixture(directory, 'ambient') if kind == 'model-grid' else create_lighting_fixture(directory)
        shader = directory/'baseq3/scripts/q3mapx_tests.shader'
        original = shader.read_text(encoding='utf-8')
        definitions = [s for s in re.split(r'(?m)(?=^textures/)', original) if s.strip()]
        variants = []
        for count in (0,1,2,7,13,31,63,127,255):
            variants.append((f'pad-{count}', padding(count)+original))
        variants.append(('reversed', '\n'.join(reversed(definitions))))
        # Deliberately conflicting *later* duplicate: existing first-definition
        # precedence must survive the ordering repair.
        variants.append(('duplicate', original+'\ntextures/q3mapx/stone\n{\n surfaceparm nodraw\n}\n'))
        expected_bsp = expected_full = expected_semantic = expected_prt = None
        reference_variants = set()
        cases = []
        try:
            for label, text in variants:
                shader.write_text(text,encoding='utf-8')
                for workers in (1,4):
                    name = f'{label}-{workers}'
                    base = ['-game','quake3','-fs_basepath',directory,'-fs_homepath',directory/'home','-threads',workers]
                    options = ['-meta','-patchmeta'] if kind == 'lighting-patch' else ['-meta']
                    timing = run(compiler,[*base,*options,source],directory,name+'-bsp')
                    compiled = Bsp(source.with_suffix('.bsp'))
                    assert not compiled.lump(14) and not unassigned_uv_vertices(compiled.lump(10)), name
                    current, meanings = payloads(compiled), semantic(compiled)
                    portal_hash = digest(source.with_suffix('.prt').read_bytes())
                    if expected_bsp is None:
                        expected_bsp, expected_semantic, expected_prt = current, meanings, portal_hash
                        (directory/'canonical.bsp').write_bytes(compiled.data)
                    assert current == expected_bsp, (kind,name,'shader allocation changed BSP payloads')
                    assert meanings == expected_semantic and portal_hash == expected_prt
                    case = {'variant':label,'workers':workers,'bsp_seconds':timing['seconds'],'bsp_lumps':current,'prt_sha256':portal_hash}
                    if label in ('pad-0','reversed'):
                        for stage, stage_options in [('vis',['-vis','-reproducible']),('light',['-light','-fast','-samples','2'])]:
                            run(compiler,[*base,*stage_options,source],directory,name+'-'+stage,timeout=120)
                        full = payloads(Bsp(source.with_suffix('.bsp')))
                        if expected_full is None: expected_full = full
                        assert full == expected_full, (kind,name,'shader allocation or workers changed final BSP')
                        case['final_lumps'] = full
                    if reference and workers == 1:
                        run(reference,[*base,*options,source],directory,name+'-reference')
                        old = Bsp(source.with_suffix('.bsp'))
                        (directory/(name+'-reference.bsp')).write_bytes(old.data)
                        assert semantic(old,initialize_unused_uvs=True) == expected_semantic, (kind,name,'repair changed per-surface data, ownership or leaf associations')
                        assert digest(source.with_suffix('.prt').read_bytes()) == expected_prt
                        old_payload = tuple(payloads(old)); reference_variants.add(old_payload)
                        case['reference_bsp_lumps'] = old_payload
                        case['reference_unassigned_uv_vertices'] = unassigned_uv_vertices(old.lump(10))
                    cases.append(case)
            # An independent negative control establishes that the semantic
            # comparison did not simply discard all reordered surface data.
            baseline = Bsp(directory/'canonical.bsp')
            changed = bytearray(baseline.data)
            pos = baseline.lumps[10][0]
            struct.pack_into('<f',changed,pos,struct.unpack_from('<f',changed,pos)[0]+1)
            negative = directory/'changed-vertex.bsp'; negative.write_bytes(changed)
            assert semantic(Bsp(negative)) != expected_semantic
        finally:
            shader.write_text(original,encoding='utf-8')
        records.append({'fixture':kind,'cases':cases,'reference_distinct_bsp_payloads':len(reference_variants),
                        'semantic_sha256':digest(json.dumps(expected_semantic,sort_keys=True).encode()),
                        'changed_vertex_detected':True})
    report = {'compiler_sha256':digest(compiler.read_bytes()),'reference_sha256':digest(reference.read_bytes()) if reference else None,
              'records':records,'raven_patches':raven_patches(compiler,root)}
    (root/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('56 BSP builds and 8 BSP/VIS/LIGHT pipelines: unused/reversed/duplicate shaders, 1/4-worker parity and IBSP/RBSP patch initialization passed')
    if reference: print('Reference distinct BSP layouts:',[r['reference_distinct_bsp_payloads'] for r in records],'; surface/owner/leaf semantics preserved except previously uninitialized pre-LIGHT UVs')


if __name__ == '__main__':
    main()

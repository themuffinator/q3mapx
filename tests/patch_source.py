"""Retained patch sources: pre-modifier paint, native/triangle-only recovery and integrity.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from fixtures import create_fixture
from integration import run
from patch_input import payloads
from patch_paint import painted, normalized
from patch_color_recovery import read_surfaces

MAGIC = b'Q3MAPX_PATCH_V1\0'
FOOTER = 84


def archive(data):
    assert data[-16:] == MAGIC
    size = struct.unpack_from('<I', data, len(data)-FOOTER)[0]
    payload = data[-FOOTER-size:-FOOTER]
    assert hashlib.sha256(payload).hexdigest().encode() == data[-80:-16]
    return payload


def primitives(text):
    result = []
    for body in re.findall(r'q3mapxPatchDef2\s*\{([^{}]*)\}', text):
        groups = re.findall(r'\(([^()]*)\)', body)
        dimensions = tuple(map(int, groups[0].split()[:2]))
        controls = tuple(struct.pack('<5f4B', *map(float, row.split()[:5]), *map(int, row.split()[5:])) for row in groups[1:])
        settings = tuple(re.search(r'\b'+field+r'\s+(\S+)', body)[1] for field in ('lightmapSampleSize','vertexRGB','paintSubdivisions'))
        result.append((body.split()[0], dimensions, settings, controls))
    return sorted(result)


def render(data, alpha=False):
    rows = []
    for surface in read_surfaces(data):
        if surface['shader'] != 'textures/q3mapx/paint' or surface['kind'] == 2: continue
        for xyz, st, color in surface['controls']:
            world = tuple(a+b for a,b in zip(xyz,surface['origin']))
            rows.append((*world,*st,*(color[3:4] if alpha else color[:4])))
    return sorted(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True, type=Path)
    parser.add_argument('--work-dir', required=True, type=Path)
    parser.add_argument('--reference', type=Path)
    args = parser.parse_args()
    exe, root = args.compiler.resolve(), args.work_dir.resolve()
    root.mkdir(parents=True,exist_ok=True)
    source = create_fixture(root,patch=False)
    plain = source.read_text(encoding='utf-8')
    scripts = source.parent.parent/'scripts/q3mapx_tests.shader'
    base_shaders = scripts.read_text(encoding='utf-8')
    raven = source.parent.parent/'shaders'; raven.mkdir(exist_ok=True)
    (raven/'shaderlist.txt').write_text('q3mapx_tests\n',encoding='utf-8')
    cases, failure_cases = [], []
    carriers = {}
    for game in ('quake3','ja'):
        base = ['-game',game,'-fs_basegame','baseq3','-fs_basepath',root,'-fs_homepath',root/'home','-threads',1]
        for mode in ('material','lighting'):
            for solid in (False,True):
                for modifier in (False,True):
                    label = f'{game}-{mode}-{solid}-{modifier}'
                    shaders = base_shaders+'\ntextures/q3mapx/paint\n{\n'
                    if not solid: shaders += 'surfaceparm nonsolid\n'
                    if modifier: shaders += 'q3map_colorMod scale ( 0.5 1 0.25 )\nq3map_alphaMod scale 0.5\nq3map_tcMod translate 2 3\n'
                    shaders += '{ map $whiteimage rgbGen vertex alphaGen vertex }\n}\n'
                    scripts.write_text(shaders,encoding='utf-8'); (raven/'q3mapx_tests.shader').write_text(shaders,encoding='utf-8')
                    world = painted(mode=mode,subdivisions=8,size=7,curved=True)
                    door = painted(mode=mode,subdivisions=4,size=13,curved=True,origin=(96,-48,144),extent=64)
                    text = plain.replace('"message" "q3mapx regression"\n','"message" "q3mapx regression"\n'+world)
                    text = text.replace('"targetname" "test_door"\n','"targetname" "test_door"\n'+door)
                    source.write_text(text,encoding='utf-8')
                    run(exe,[*base,source],root,label+'-bsp')
                    raw = source.with_suffix('.bsp').read_bytes()
                    retained = archive(raw)
                    assert sum(s['kind']==2 for s in read_surfaces(raw)) == (2 if solid else 0)
                    run(exe,[*base,'-no-patch-source',source],root,label+'-no-archive')
                    without = source.with_suffix('.bsp').read_bytes()
                    assert not without.endswith(MAGIC) and normalized(payloads(raw)) == normalized(payloads(without))
                    if args.reference:
                        run(args.reference.resolve(),[*base,source],root,label+'-previous')
                        assert normalized(payloads(source.with_suffix('.bsp').read_bytes())) == normalized(payloads(raw))
                    source.with_suffix('.bsp').write_bytes(raw)
                    snapshots = [('bsp',raw)]
                    for stage,options in (('vis',['-vis']),('light',['-light','-fast']),('relight',['-light','-fast','-bounce',1])):
                        run(exe,[*base,*options,source],root,label+'-'+stage)
                        data = source.with_suffix('.bsp').read_bytes()
                        assert archive(data) == retained
                        snapshots.append((stage,data))
                    # Entity-only updates must preserve the old compiled paint,
                    # even when the adjacent source now contains different paint.
                    source.write_text(text.replace('paintSubdivisions 8','paintSubdivisions 32'),encoding='utf-8')
                    run(exe,[*base,'-onlyents',source],root,label+'-onlyents')
                    data = source.with_suffix('.bsp').read_bytes()
                    assert archive(data) == retained
                    snapshots.append(('onlyents',data))
                    for n,(stage,data) in enumerate(snapshots):
                        bsp = source.with_name(label+'-'+stage+'.bsp'); bsp.write_bytes(data)
                        bsp.with_suffix('.map').write_bytes(b'not source metadata')
                        bsp.with_suffix('.srf').write_bytes(b'not source metadata')
                        output = bsp.with_name(bsp.stem+'-source.map')
                        run(exe,[*base,'-decompile','-format',('map','map_bp','map_220')[n%3],'-patch-recovery','source','-o',output,bsp],root,output.stem)
                        assert primitives(output.read_text(encoding='utf-8')) == primitives(text), label+'-'+stage
                        assert output.read_bytes().count(b'q3mapxPatchDef2') == 2
                        report = json.loads(Path(str(output)+'.recovery.json').read_text(encoding='utf-8'))
                        assert report['patches'] == report['patch_recovery']['restored_source_patches'] == 2
                        assert report['patch_recovery']['geometry_binding_verified']
                        run(exe,[*base,output],root,output.stem+'-rebuild')
                        rebuilt = output.with_suffix('.bsp').read_bytes()
                        assert render(rebuilt) == render(raw), label+'-'+stage
                        run(exe,[*base,'-light','-fast',output],root,output.stem+'-light')
                        assert render(output.with_suffix('.bsp').read_bytes(), mode=='lighting') == render(rebuilt,mode=='lighting')
                        assert bsp.read_bytes() == data
                        cases.append(dict(case=label+'-'+stage,source_controls=True,rebuild_samples=True,retained_archive_sha256=hashlib.sha256(retained).hexdigest()))
                    carriers[game] = (base,raw)

    # Corrupt lengths, payloads, records and geometry must never replace a MAP
    # or report with invented source controls. Checksummed syntax is not enough
    # to override the geometry binding or allocation limits.
    for game,(base,raw) in carriers.items():
        payload = archive(raw)
        start = len(raw)-len(payload)-FOOTER
        transformed=source.with_name(game+'-transform.bsp'); transformed.write_bytes(raw)
        run(exe,[*base,'-scale',2,transformed],root,game+'-scale')
        assert not transformed.with_name(transformed.stem+'_s.bsp').read_bytes().endswith(MAGIC)
        assert transformed.read_bytes()==raw
        def with_payload(changed):
            return raw[:start]+changed+struct.pack('<I',len(changed))+hashlib.sha256(changed).hexdigest().encode()+MAGIC
        variants = [('missing',raw[:start],b'No retained patch source'),
                    ('bad-checksum',raw[:start]+bytes([payload[0]^1])+raw[start+1:],b'checksum mismatch'),
                    ('short-payload',with_payload(payload[:20]),b'patch source'),
                    ('trailing-data',with_payload(payload+b'junk'),b'Trailing patch source'),
                    ('bad-version',with_payload(struct.pack('<I',99)+payload[4:]),b'Unsupported patch source')]
        profile_length = struct.unpack_from('<I',payload,4)[0]
        digest = 12+profile_length
        count = digest+64
        record = count+4
        shader_length = struct.unpack_from('<I',payload,record+32)[0]
        controls = record+36+shader_length
        width,height = struct.unpack_from('<2I',payload,record+12)
        links = controls+width*height*24
        for label,offset,value in (
            ('count',count,10001), ('model',record,0xffffffff),
            ('entity',record+4,0xffffffff), ('primitive',record+8,0xffffffff),
            ('width',record+12,0x7fffffff), ('height',record+16,2),
            ('mode',record+20,99), ('subdivisions',record+24,3),
            ('density',record+28,0x7fffffff), ('shader-length',record+32,0xffffffff),
            ('nan-position',controls,0x7fc00000), ('inf-uv',controls+12,0x7f800000),
            ('links',links,2000001), ('surface',links+4,0xffffffff)):
            changed=bytearray(payload); struct.pack_into('<I',changed,offset,value)
            variants.append((label,with_payload(changed),b'patch source'))
        for label,offset,value in (('digest',digest,ord('z')),('shader-control',record+36+10,1),
                                   ('shader-prefix',record+36,ord('x'))):
            changed=bytearray(payload); changed[offset]=value
            variants.append((label,with_payload(changed),b'patch source'))
        changed=bytearray(raw); struct.pack_into('<I',changed,len(raw)-FOOTER,0xffffffff)
        variants.append(('length-limit',changed,b'payload length'))
        changed=bytearray(raw)
        vertex=struct.unpack_from('<i',changed,8+10*8)[0]
        old=struct.unpack_from('<f',changed,vertex)[0]; struct.pack_into('<f',changed,vertex,old+1)
        variants.append(('stale-geometry',changed,b'does not match compiled geometry'))
        for label,data,message in variants:
            bsp=source.with_name(game+'-'+label+'.bsp'); bsp.write_bytes(data)
            output=root/'preserved.map'; report=root/'preserved.json'
            output.write_bytes(b'previous map'); report.write_bytes(b'previous report')
            result=subprocess.run([str(exe),*map(str,[*base,'-decompile','-patch-recovery','source','-o',output,'-report',report,bsp])],cwd=root,capture_output=True,timeout=60)
            log=result.stdout+result.stderr; (root/(game+'-'+label+'.log')).write_bytes(log)
            assert result.returncode==1 and message in log,(label,log[-4000:])
            assert output.read_bytes()==b'previous map' and report.read_bytes()==b'previous report' and bsp.read_bytes()==data
            failure_cases.append(game+'-'+label)
    (root/'validation.json').write_text(json.dumps(dict(cases=cases,failures=failure_cases,geometry_transform_drops_archive=len(carriers),reference_native_lump_parity=bool(args.reference)),indent=2)+'\n',encoding='utf-8')
    print(f'{len(cases)} exact source/rebuild cases and {len(failure_cases)} integrity/output-preservation cases passed')


if __name__ == '__main__': main()

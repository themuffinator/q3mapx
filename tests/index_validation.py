"""Shared index-span validation through the native loader. GPL-3.0-or-later."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def indexed_bsp(indices, spans):
    """Minimal IBSP46: spans are (first index, index count, local vertex count).

    Original generated geometry only. Deliberately permits unused index values
    and surface overlap: neither is prohibited by the file format.
    """
    vertices = max((span[2] for span in spans), default=0)
    lumps = [b"" for _ in range(17)]
    lumps[0] = b'{\n"classname" "worldspawn"\n}\n\0'
    lumps[1] = b"textures/q3mapx/generated\0".ljust(64,b"\0") + struct.pack("<2i",0,1)
    lumps[4] = struct.pack("<12i",-1,0,-128,-128,-128,128,128,128,0,0,0,0)
    lumps[7] = struct.pack("<6f4i",-128,-128,-128,128,128,128,0,len(spans),0,0)
    lumps[10] = b"".join(struct.pack("<10f4B",float(i%3==1),float(i%3==2),0,0,0,0,0,0,0,1,255,255,255,255) for i in range(vertices))
    lumps[11] = struct.pack(f"<{len(indices)}i",*indices)
    lumps[13] = b"".join(struct.pack("<8i",0,-1,1,0,count,first,length,-1)+bytes(72) for first,length,count in spans)
    data = bytearray(b"IBSP"+struct.pack("<i",46)+bytes(17*8))
    for i,lump in enumerate(lumps):
        data.extend(b"\0")  # exercise supported unaligned native records
        struct.pack_into("<2i",data,8+i*8,len(data),len(lump))
        data.extend(lump)
    return bytes(data)


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--compiler",type=Path,required=True)
    parser.add_argument("--work-dir",type=Path,required=True)
    args=parser.parse_args()
    exe,root=args.compiler.resolve(strict=True),args.work_dir.resolve()
    root.mkdir(parents=True,exist_ok=True)
    records=[]

    def check(name,indices,spans,*,bad=None):
        data=indexed_bsp(indices,spans)
        source=root/f"{name}.bsp"
        source.write_bytes(data)
        for force in (False,True):
            command=[str(exe),"-game","quake3","-fs_basepath",str(root),"-fs_homepath",str(root/"home"),
                     "-threads","1",*(["-force"] if force else []),"-info",str(source)]
            result=subprocess.run(command,cwd=root,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=25)
            text=result.stdout.decode(errors="replace")
            (root/f"{name}-{'forced' if force else 'normal'}.log").write_text(text,encoding="utf-8")
            assert result.returncode == (1 if bad else 0),(name,result.returncode,text)
            if bad:
                surface,position=bad
                expected=f"triangle vertex {surface} references {indices[position]} (limit {spans[surface][2]})"
                assert expected in text,(name,expected,text)
            assert source.read_bytes()==data,"Validation modified its source"
        records.append({"case":name,"indices":len(indices),"surfaces":len(spans),
                        "expanded_index_references":sum(s[1] for s in spans),"sha256":hashlib.sha256(data).hexdigest(),
                        "expected_first_error":list(bad) if bad else None,"result":"passed"})

    shared=list(range(64))*192
    check("repeated-valid",shared,[(0,len(shared),64)]*1000)
    check("different-local-counts",shared,[(0,len(shared),64)]*4+[(120,900,128),(120,900,63)],bad=(5,127))
    # Even with the lookup built, an invalid value outside a slice cannot make
    # that slice invalid, including when it shares the same cache block.
    sparse=[-2147483648]+[0,1,2]*4096+[2147483647]
    warm=[(1,len(sparse)-2,3)]*4
    check("unused-invalid-values",sparse,warm+[(2,3,3),(len(sparse)-4,3,3),(len(sparse),0,0)])
    check("negative-prefix",sparse,warm+[(0,3,3)],bad=(4,0))
    check("overflow-suffix",sparse,warm+[(len(sparse)-3,3,3)],bad=(4,len(sparse)-1))
    for position in (63,64,65,126,127,128,4095,4096,8191):
        data=[0]*9000
        data[position]=17
        check(f"tight-count-{position}",data,[(0,9000,18)]*4+[(1,8997,17)],bad=(4,position))
    check("empty",[],[(0,0,0)])
    check("zero-vertices",[0,0,0],[(0,3,0)],bad=(0,0))
    # Hundreds of millions of implied references in a modest physical file.
    check("overlap-stress",[0,1,2]*20_000,[(0,60_000,3)]*10_000)
    (root/"validation.json").write_text(json.dumps({"schema_version":1,"compiler_sha256":hashlib.sha256(exe.read_bytes()).hexdigest(),
        "cases":records,"forced_and_normal":True,"source_bytes_preserved":True},indent=2)+"\n",encoding="utf-8")
    print(f"{len(records)*2} shared-index native-loader checks passed")


if __name__=="__main__":
    main()

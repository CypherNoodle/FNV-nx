"""Native uncompressed mip patches versus an independent byte model."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import subprocess

U32=C.c_uint32
class Patch(C.Structure):
    _fields_=[(n,U32) for n in ('width','height','left','top','copy_width','copy_height','format','source_pitch')]+[('source',C.c_void_p),('source_bytes',C.c_size_t)]

def run(args):
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    native=Path(__file__).resolve().parents[1]/'native';dll=out/'texture_upload.dll'
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    inputs=[Path(__file__).resolve(),native/'d3d9_texture_upload.c',native/'d3d9_texture_upload.h'];frozen={str(p):sha(p) for p in inputs}
    subprocess.run([*args.cc,'-shared','-O2','-std=c11','-Wall','-Wextra','-Werror','-Wl,--export-all-symbols',str(inputs[1]),'-o',str(dll)],check=True)
    lib=C.CDLL(str(dll));fn=lib.fnv_d3d9_texture_rgba8
    fn.argtypes=[C.POINTER(Patch),C.c_void_p,C.c_size_t,U32];fn.restype=U32
    rng=random.Random(0x7E87);formats=(21,22,23,28,50)
    for case in range(3000):
        fmt=formats[case%5];unit={21:4,22:4,23:2,28:1,50:1}[fmt]
        w,h=rng.randint(1,20),rng.randint(1,17);left,top=rng.randrange(w),rng.randrange(h)
        cw,ch=rng.randint(1,w-left),rng.randint(1,h-top);sp=cw*unit+rng.randrange(13);dp=w*4+rng.randrange(13)
        source=rng.randbytes((ch-1)*sp+cw*unit);src=C.create_string_buffer(source)
        expected=bytearray([0xa5])*(h*dp+16);dst=C.create_string_buffer(bytes(expected),len(expected))
        p=Patch(w,h,left,top,cw,ch,fmt,sp,C.addressof(src),len(source))
        for y in range(ch):
            for x in range(cw):
                v=source[y*sp+x*unit:y*sp+(x+1)*unit]
                if fmt in (21,22):rgba=[v[2],v[1],v[0],v[3] if fmt==21 else 255]
                elif fmt==23:
                    bits=int.from_bytes(v,'little');rgba=[round((bits>>11)*255/31),round(((bits>>5)&63)*255/63),round((bits&31)*255/31),255]
                elif fmt==28:rgba=[0,0,0,v[0]]
                else:rgba=[v[0]]*3+[255]
                offset=(top+y)*dp+(left+x)*4;expected[offset:offset+4]=bytes(rgba)
        assert not fn(C.byref(p),dst,h*dp,dp) and dst.raw==bytes(expected)
    # Validation must be transactional even when the invalid part is last row.
    source=C.create_string_buffer(bytes(range(32)));dst=C.create_string_buffer(b'\xa5'*64,64)
    p=Patch(4,4,1,1,2,2,21,16,C.addressof(source),24);negative=0
    def reject(size=64,pitch=16,destination=dst):
        nonlocal negative
        before=dst.raw
        assert fn(C.byref(p),destination,size,pitch)==0x8876086c and dst.raw==before
        negative+=1
    for field,value in (('width',0),('height',0),('left',4),('top',4),('copy_width',0),('copy_height',0),('copy_width',4),('copy_height',4),('format',0),('format',0x31545844),('source_pitch',7),('source_bytes',23),('source',None),('width',0xffffffff),('height',0xffffffff),('left',0xffffffff),('source_pitch',0xffffffff)):
        old=getattr(p,field);setattr(p,field,value);reject();setattr(p,field,old)
    reject(size=63);reject(pitch=15);reject(destination=None)
    p.source=C.addressof(dst)+4;p.source_bytes=24;reject()
    p.source=C.addressof(dst)-4;reject()
    p.source=(1<<(C.sizeof(C.c_void_p)*8))-8;reject()
    assert fn(None,dst,64,16)==0x8876086c
    assert all(sha(Path(p))==h for p,h in frozen.items())
    report=dict(status='PASS',differential_cases=3000,formats=list(formats),negative_cases=negative,
                source_sha256=frozen,dll_sha256=sha(dll),gpu_rendered=False,game_executed=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

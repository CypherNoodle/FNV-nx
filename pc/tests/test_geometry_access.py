"""Differential indexed/nonindexed D3D9 byte ranges; no GPU or game."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess

U32=C.c_uint32
U64=C.c_uint64
class Stream(C.Structure):
    _fields_=[('bytes',U64),('offset',U32),('stride',U32),('frequency',U32)]
class Input(C.Structure):
    _fields_=[('streams',Stream*16),('elements',C.c_void_p),('element_count',U32),
              ('indices',C.c_void_p),('index_bytes',U64)]+[(n,U32) for n in
              ('index_size','indexed','primitive','base_bits','min_vertex','vertices','start','primitives')]
class Range(C.Structure):
    _fields_=[('offset',U64),('bytes',U64)]
class Access(C.Structure):
    _fields_=[('streams',Range*16),('indices',Range)]+[(n,U32) for n in ('count','first_vertex','last_vertex','stream_mask')]

def run(args):
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    native=Path(__file__).resolve().parents[1]/'native';dll=out/'geometry.dll'
    inputs=[Path(__file__).resolve(),native/'d3d9_geometry.c',native/'d3d9_geometry.h']
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    frozen={str(p):sha(p) for p in inputs}
    subprocess.run([*args.cc,'-shared','-O2','-std=c11','-Wall','-Wextra','-Werror',
                    '-Wl,--export-all-symbols',str(inputs[1]),'-o',str(dll)],check=True)
    lib=C.CDLL(str(dll));fn=lib.fnv_d3d9_geometry_validate
    fn.argtypes=[C.POINTER(Input),C.POINTER(Access)];fn.restype=U32
    rng=random.Random(0xD3D900);cases=0
    for _ in range(4000):
        primitive=rng.randint(1,6);primitives=rng.randint(1,30)
        count={1:primitives,2:primitives*2,3:primitives+1,4:primitives*3,5:primitives+2,6:primitives+2}[primitive]
        indexed=rng.randrange(2);width=rng.choice((2,4));start=rng.randrange(8)
        values=[rng.randrange(20) for _ in range(count)];base=rng.randint(-5,5)
        packed=b'!'+bytes(start*width)+struct.pack('<'+('H' if width==2 else 'I')*count,*values)
        storage=C.create_string_buffer(packed)
        decl=C.create_string_buffer(struct.pack('<HH4B',0,0,1,0,0,0)+struct.pack('<HH4B',0,8,1,0,5,0)+struct.pack('<HH4B',255,0,17,0,0,0))
        i=Input();i.elements=C.addressof(decl);i.element_count=3
        i.streams[0]=Stream(rng.randrange(700),rng.randrange(20),rng.choice((0,4,16,20)),1)
        i.indexed=indexed;i.primitive=primitive;i.primitives=primitives;i.start=start
        i.indices=C.addressof(storage)+1;i.index_bytes=len(packed)-1-rng.randrange(3)
        i.index_size=width;i.base_bits=base&0xffffffff;i.min_vertex=rng.randrange(3);i.vertices=rng.randrange(15,25)
        lo=min(values)+base if indexed else start
        hi=max(values)+base if indexed else start+count-1
        valid=lo>=0 and hi<=0xffffffff
        if indexed:valid &= all(i.min_vertex<=v<i.min_vertex+i.vertices for v in values) and (start+count)*width<=i.index_bytes
        first=i.streams[0].offset+lo*i.streams[0].stride
        end=i.streams[0].offset+hi*i.streams[0].stride+16
        valid &= end<=i.streams[0].bytes
        result=Access();C.memset(C.byref(result),0xa5,C.sizeof(result));before=bytes(result)
        hr=fn(C.byref(i),C.byref(result));assert (hr==0)==bool(valid),(hr,valid,lo,hi)
        if valid:
            assert (result.first_vertex,result.last_vertex,result.count,result.stream_mask)==(lo,hi,count,1)
            assert (result.streams[0].offset,result.streams[0].bytes)==(first,end-first)
            assert (result.indices.offset,result.indices.bytes)==((start*width,count*width) if indexed else (0,0))
        else:assert hr==0x8876086c and bytes(result)==before
        cases+=1
    # Types, malformed END, duplicate semantics, missing stream, instancing,
    # unsigned arithmetic extremes and negative base: all fail without output writes.
    i.indexed=0;i.start=0;i.primitive=4;i.primitives=1;i.streams[0]=Stream(64,0,16,1)
    original=bytes(decl);negatives=0
    def reject():
        nonlocal negatives
        result=Access();C.memset(C.byref(result),0xa5,C.sizeof(result));before=bytes(result)
        assert fn(C.byref(i),C.byref(result))==0x8876086c and bytes(result)==before
        negatives+=1
    for at,value in ((4,17),(5,1),(6,14),(7,16),(8,16),(16,0),(20,16),(23,1)):
        C.memmove(decl,original,len(original));decl[at]=bytes([value]);reject()
    C.memmove(decl,original,len(original));decl[14]=b'\0';reject()
    C.memmove(decl,original,len(original))
    i.streams[0].frequency=0;reject();i.streams[0].frequency=0x40000001;reject();i.streams[0].frequency=1
    for field,value in (('primitives',0),('primitives',0xffffffff),('primitive',0),('primitive',7),('start',0xffffffff),('element_count',1),('element_count',66),('indexed',2)):
        old=getattr(i,field);setattr(i,field,value);reject();setattr(i,field,old)
    i.indexed=1;i.index_size=3;reject();i.index_size=2;i.base_bits=0x80000000;reject()
    # All declared format widths, two streams, constant stride zero, exact end
    # bounds and high unsigned indices with signed base -2^31.
    format_cases=0
    for kind,size in enumerate((4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8)):
        raw=struct.pack('<HH4B',0,0,kind,0,0,0)+struct.pack('<HH4B',1,4,1,0,5,0)+struct.pack('<HH4B',255,0,17,0,0,0)
        C.memmove(decl,raw,len(raw));i.indexed=0;i.primitive=1;i.primitives=1;i.start=7
        i.streams[0]=Stream(size,0,0,1);i.streams[1]=Stream(7*20+12,0,20,1)
        result=Access();assert not fn(C.byref(i),C.byref(result)) and result.stream_mask==3
        assert (result.streams[0].offset,result.streams[0].bytes)==(0,size)
        assert (result.streams[1].offset,result.streams[1].bytes)==(144,8)
        i.streams[0].bytes-=1;reject();format_cases+=1
    C.memmove(decl,original,len(original));i.indexed=1;i.index_size=4;i.start=0;i.primitive=1;i.primitives=2
    large=C.create_string_buffer(struct.pack('<2I',0x80000000,0x80000001))
    i.indices=C.addressof(large);i.index_bytes=8;i.base_bits=0x80000000;i.min_vertex=0x80000000;i.vertices=2
    i.streams[0]=Stream(32,0,16,1)
    result=Access();assert not fn(C.byref(i),C.byref(result)) and result.first_vertex==0 and result.last_vertex==1
    i.base_bits=0x7fffffff;reject()
    i.indexed=0;i.start=0xffffffff;i.primitives=1;i.streams[0]=Stream(0xffffffffffffffff,0xffffffff,0xffffffff,1)
    assert not fn(C.byref(i),C.byref(result))
    assert result.streams[0].offset==0xffffffff+0xffffffff*0xffffffff
    assert fn(None,C.byref(result))==0x8876086c and fn(C.byref(i),None)==0x8876086c
    assert all(sha(Path(p))==h for p,h in frozen.items())
    report=dict(status='PASS',differential_cases=cases,format_cases=format_cases,negative_cases=negatives,source_sha256=frozen,
                dll_sha256=sha(dll),gpu_rendered=False,game_executed=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

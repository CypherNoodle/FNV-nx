"""BC1/2/3 decoder oracles, independent box model and complete DDS mip chains."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import time


def run(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(exist_ok=False)
    inputs=[Path(__file__).resolve(),pc/'tests/bc3_mip_host.c',pc/'native/bc_mip_chain.inc',pc/'native/dds_image_info.h',pc/'third_party/stb_dxt/stb_dxt.h',pc/'third_party/stb_dxt/provenance.json',Path('C:/Windows/System32/d3dx9_38.dll')]
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    frozen={str(p):sha(p) for p in inputs}
    provenance=json.loads(inputs[-2].read_text());assert sha(inputs[-3])==provenance['sha256']
    command=[*a.cc,'-shared','-O2','-std=c11','-Wall','-Wextra','-Werror','-ffp-contract=off','-fno-strict-aliasing','-Wl,--export-all-symbols','-I'+str(pc/'native'),str(pc/'tests/bc3_mip_host.c'),'-ld3d9','-luser32','-o',str(out/'bc3.dll')]
    r=subprocess.run(command,capture_output=True,text=True);(out/'compiler.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
    dll=C.CDLL(str(out/'bc3.dll'));u32=C.c_uint32
    for name in ('decode_block','encode_block','native_decode'):getattr(dll,name).argtypes=[C.c_void_p,C.c_void_p]
    dll.half_image.argtypes=[C.c_void_p,u32,u32]
    dll.complete.argtypes=[C.c_void_p,u32,u32,u32,u32,u32,u32,C.POINTER(C.c_void_p),C.POINTER(u32)];dll.complete.restype=u32
    dll.free_chain.argtypes=[C.c_void_p]
    for name in ('decode_format','encode_format','native_decode_format'):getattr(dll,name).argtypes=[C.c_void_p,u32,C.c_void_p]
    dll.complete_format.argtypes=[C.c_void_p,u32,u32,u32,u32,u32,u32,u32,C.POINTER(C.c_void_p),C.POINTER(u32)];dll.complete_format.restype=u32
    assert dll.native_init();rng=random.Random(0xbca31);decoder_max=0
    def decode(block):
        src=C.create_string_buffer(block);dst=C.create_string_buffer(64);dll.decode_block(src,dst);return dst.raw
    for i in range(2000):
        block=bytearray(rng.randbytes(16));c0,c1=struct.unpack_from('<HH',block,8)
        # D3DX's surface utility treats c0<=c1 as BC1-style colors even for
        # BC3. Use canonical four-color blocks for this native comparison.
        if c0<c1:c0,c1=c1,c0
        if c0==c1:
            if c0:c1-=1
            else:c0=1
        struct.pack_into('<HH',block,8,c0,c1);block=bytes(block);src=C.create_string_buffer(block);expected=C.create_string_buffer(64);assert dll.native_decode(src,expected)
        got=decode(block);diff=max(abs(x-y) for x,y in zip(got,expected.raw));decoder_max=max(decoder_max,diff);assert diff<=1,(i,diff,block.hex())
    # BC3 always has four colors, including c0<=c1; alpha is separate.
    for c0,c1,wanted in [(0,65535,170),(0,0,0),(65535,65535,255)]:
        block=bytes((255,0))+bytes(6)+struct.pack('<HHI',c0,c1,0xffffffff)
        assert decode(block)==bytes((wanted,wanted,wanted,255))*16
    def half(src,w,h):
        nw,nh=max(1,w//2),max(1,h//2);dst=bytearray(nw*nh*4)
        for y in range(nh):
            for x in range(nw):
                for c in range(4):
                    samples=[src[(min(h-1,2*y+dy)*w+min(w-1,2*x+dx))*4+c] for dy in range(2) for dx in range(2)]
                    dst[(y*nw+x)*4+c]=(sum(samples)+2)//4
        return bytes(dst)
    for i in range(1000):
        w,h=1<<rng.randrange(6),1<<rng.randrange(6);src=rng.randbytes(w*h*4);expected=half(src,w,h);buf=C.create_string_buffer(src);dll.half_image(buf,w,h);assert buf.raw[:len(expected)]==expected
    def dds(w,h,levels):
        header=bytearray(128);values={0:0x20534444,4:124,8:0x21007,12:h,16:w,28:levels,76:32,80:4,84:0x35545844,108:0x401008}
        for offset,value in values.items():struct.pack_into('<I',header,offset,value)
        # Each supplied level has a distinct flat color. Missing levels must
        # be derived from the last supplied level, never from level zero.
        for level in range(levels):
            rgba=bytes((32+level*40,96+level*8,224-level*24,40+level*35))*16;src=C.create_string_buffer(rgba);block=C.create_string_buffer(16);dll.encode_block(src,block)
            header.extend(block.raw*(((w+3)//4)*((h+3)//4)));w=max(1,w//2);h=max(1,h//2)
        return bytes(header)
    cases=0;negatives=0
    def complete(data,w,h,levels,maximum,budget=8<<20):
        src=C.create_string_buffer(data);ptr=C.c_void_p(123);size=u32(99);hr=dll.complete(src,len(data),w,h,levels,maximum,budget,C.byref(ptr),C.byref(size))
        if hr:assert not ptr.value and not size.value;return hr,None
        value=C.string_at(ptr,size.value);dll.free_chain(ptr);return hr,value
    for w,h in [(4,4),(8,16),(32,32),(1,32),(32,1),(64,32)]:
        maximum=max(w,h).bit_length()
        for supplied in range(1,maximum):
            data=dds(w,h,supplied);hr,result=complete(data,w,h,supplied,maximum);assert not hr
            assert result[128:len(data)]==data[128:] and struct.unpack_from('<I',result,28)[0]==maximum
            lw,lh=max(1,w>>(supplied-1)),max(1,h>>(supplied-1));last=decode(data[-16:]);color=last[:4];offset=len(data)
            for level in range(supplied,maximum):
                lw,lh=max(1,lw//2),max(1,lh//2);length=((lw+3)//4)*((lh+3)//4)*16
                for at in range(offset,offset+length,16):
                    pixel=decode(result[at:at+16]);assert max(abs(pixel[i]-color[i%4]) for i in range(64))<=3
                offset+=length
            assert offset==len(result);cases+=1
            for bad in ('budget','truncated','npot','maximum'):
                hr,_=complete(data[:-1] if bad=='truncated' else data,3 if bad=='npot' else w,h,supplied,maximum+1 if bad=='maximum' else maximum,1 if bad=='budget' else 8<<20)
                assert hr;negatives+=1
    family_cases=0;family_native=0;cutout_checks=0
    for fmt in (0x31545844,0x33545844):
        for i in range(1000):
            block=bytearray(rng.randbytes(16));off=0 if fmt==0x31545844 else 8
            if off:
                c0,c1=struct.unpack_from('<HH',block,off)
                if c0<c1:c0,c1=c1,c0
                if c0==c1:c0,c1=max(1,c0),max(0,c1-1)
                struct.pack_into('<HH',block,off,c0,c1)
            source=C.create_string_buffer(bytes(block));got=C.create_string_buffer(64);wanted=C.create_string_buffer(64)
            dll.decode_format(source,fmt,got);assert dll.native_decode_format(source,fmt,wanted)
            assert max(abs(x-y) for x,y in zip(got.raw,wanted.raw))<=1;family_native+=1
        for w,h in [(4,4),(8,16),(32,32),(1,32),(32,1),(64,32)]:
            maximum=max(w,h).bit_length()
            for supplied in range(1,maximum):
                data=bytearray(dds(w,h,1)[:128]);struct.pack_into('<I',data,28,supplied);struct.pack_into('<I',data,84,fmt)
                unit=8 if fmt==0x31545844 else 16
                for level in range(supplied):
                    # Opaque red, or fully transparent BC1, exercise exact
                    # opacity endpoints without compressor-dependent colors.
                    alpha=0 if fmt==0x31545844 and level%2==0 else 255
                    source=C.create_string_buffer(bytes((255,0,0,alpha))*16);block=C.create_string_buffer(unit);dll.encode_format(source,fmt,block)
                    data.extend(block.raw*(((max(1,w>>level)+3)//4)*((max(1,h>>level)+3)//4)))
                source=C.create_string_buffer(bytes(data));ptr=C.c_void_p();size=u32()
                assert not dll.complete_format(source,len(data),w,h,supplied,maximum,fmt,8<<20,C.byref(ptr),C.byref(size))
                result=C.string_at(ptr,size.value);dll.free_chain(ptr);assert result[128:len(data)]==data[128:]
                offset=len(data)
                for level in range(supplied,maximum):
                    length=((max(1,w>>level)+3)//4)*((max(1,h>>level)+3)//4)*unit
                    for at in range(offset,offset+length,unit):
                        block=C.create_string_buffer(result[at:at+unit]);rgba=C.create_string_buffer(64);dll.decode_format(block,fmt,rgba)
                        for pixel in range(16):
                            got=rgba.raw[pixel*4:pixel*4+4];expected=(0,0,0,0) if not alpha else (255,0,0,255)
                            assert max(abs(x-y) for x,y in zip(got,expected))<=3
                    offset+=length
                assert offset==len(result);family_cases+=1
        if fmt==0x31545844:
            # Half of a BC1 block transparent, half opaque. Binary alpha must
            # survive encoding; invisible colors must not skew opaque red.
            rgba=bytes((255,0,0,255))*8+bytes((0,255,255,0))*8;source=C.create_string_buffer(rgba);block=C.create_string_buffer(8);decoded=C.create_string_buffer(64)
            dll.encode_format(source,fmt,block);dll.decode_format(block,fmt,decoded)
            assert [decoded.raw[i*4+3] for i in range(16)]==[255]*8+[0]*8
            assert all(decoded.raw[i*4]>250 and decoded.raw[i*4+1]<4 and decoded.raw[i*4+2]<4 for i in range(8));cutout_checks+=1
            header=bytearray(dds(4,4,1)[:128]);struct.pack_into('<I',header,84,fmt);data=bytes(header)+block.raw
            source=C.create_string_buffer(data);ptr=C.c_void_p();size=u32();assert not dll.complete_format(source,len(data),4,4,1,3,fmt,1024,C.byref(ptr),C.byref(size))
            result=C.string_at(ptr,size.value);dll.free_chain(ptr);mip=C.create_string_buffer(result[136:144]);dll.decode_format(mip,fmt,decoded)
            assert [decoded.raw[i*4+3] for i in (0,1,4,5)]==[255,255,0,0];cutout_checks+=1
    data=dds(1024,1024,1);start=time.perf_counter();hr,result=complete(data,1024,1024,1,11);elapsed=time.perf_counter()-start;assert not hr and len(result)==1398256
    dll.native_close();assert all(sha(Path(p))==h for p,h in frozen.items())
    report=dict(status='PASS',decoder_native_blocks=2000,decoder_max_channel_difference=decoder_max,exact_box_cases=1000,complete_chain_cases=cases,bc1_bc2_native_blocks=family_native,bc1_bc2_chain_cases=family_cases,bc1_cutout_checks=cutout_checks,negative_cases=negatives,desktop_1024_generation_ms=elapsed*1000,source_sha256=frozen,switch_hardware_tested=False,engine_started=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

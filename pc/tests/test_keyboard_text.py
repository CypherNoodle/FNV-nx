"""Native keyboard encoding against Python's independent Windows-1252 codec."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    native=Path(__file__).resolve().parents[1]/'native'
    source=a.output/'text.c';library=a.output/'text.dll'
    source.write_text('#include "text_cp1252.h"\n__declspec(dllexport) int encode(const unsigned char *p,size_t n,char *o,size_t c){return fnv_text_to_utf8(p,n,o,c); }\n__declspec(dllexport) int decode(const char *p,unsigned char *o,size_t c){return fnv_text_from_utf8(p,o,c); }\n')
    subprocess.run([str(a.cc),'-std=c11','-Wall','-Wextra','-Werror','-shared','-O2','-I',str(native),str(source),'-o',str(library)],check=True)
    lib=ctypes.CDLL(str(library.resolve()))
    lib.encode.argtypes=[ctypes.c_char_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t]
    lib.decode.argtypes=[ctypes.c_char_p,ctypes.c_void_p,ctypes.c_size_t]
    cases=0
    for value in range(32,256):
        raw=bytes([value]);out=ctypes.create_string_buffer(129)
        try: expected=raw.decode('cp1252').encode('utf8') if value!=127 else None
        except UnicodeDecodeError: expected=None
        assert bool(lib.encode(raw,1,out,len(out)))==bool(expected),value
        if expected:
            assert out.value==expected,value
            back=ctypes.create_string_buffer(33)
            assert lib.decode(expected,back,len(back)) and back.value==raw,value
        cases+=1
    for raw in (b'',b'Courier',b'Jos\xe9 Mu\xf1oz',b'A'*32,b'\x80'*32):
        expected=raw.decode('cp1252').encode('utf8');out=ctypes.create_string_buffer(129)
        assert lib.encode(raw,len(raw),out,len(out)) and out.value==expected
        back=ctypes.create_string_buffer(33)
        assert lib.decode(expected,back,len(back)) and back.value==raw
        assert not lib.decode(expected,back,len(raw))
        assert not lib.encode(raw,len(raw),out,len(expected))
        cases+=1
    for raw in (b'\xc0\x80',b'\xe0\x80\xaf',b'\xed\xa0\x80',b'\xc2',b'\xe2\x82',b'\x80',b'\n',b'A'*33,'\U0001f600'.encode(),'\u0416'.encode()):
        out=ctypes.create_string_buffer(33)
        assert not lib.decode(raw,out,len(out)),raw
        cases+=1
    header=native/'text_cp1252.h'
    report=dict(status='PASS',cases=cases,header_sha256=hashlib.sha256(header.read_bytes()).hexdigest())
    (a.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))

if __name__=='__main__': main()

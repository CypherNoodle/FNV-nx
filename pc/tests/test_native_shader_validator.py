"""Positive and negative Windows native shader admission checks on own HLSL."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import sys
from types import SimpleNamespace
from test_d3d9_spirv import compile_hlsl
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_native_shader_catalog import audit


def run(a):
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);raw=out/'raw';raw.mkdir()
    compiler=C.WinDLL('d3dcompiler_47.dll');keys=[];blobs=[]
    for source,target in ((b'float4 main(float4 p:POSITION):POSITION{return p;}',b'vs_2_0'),(b'float4 main():COLOR{return float4(.2,.4,.6,1);}',b'ps_2_0')):
        data=compile_hlsl(compiler,source,target);h=hashlib.sha256(data).hexdigest();(raw/(h+'.bin')).write_bytes(data);keys.append(h);blobs.append(data)
    manifest=out/'manifest.txt';manifest.write_text('\n'.join(keys)+'\n');audit(SimpleNamespace(output=out/'native',manifest=manifest,raw=raw,cc=a.cc))
    proof=json.loads((out/'native/validation.json').read_text());assert proof['accepted']==proof['total']==2
    dll=C.CDLL(str(out/'native/validator.dll'));dll.shader_validator_open.restype=C.c_uint32;dll.shader_validator_check.argtypes=[C.c_void_p,C.c_uint32];dll.shader_validator_check.restype=C.c_uint32
    assert not dll.shader_validator_open();assert dll.shader_validator_open()!=0
    negative=0
    try:
        for source in blobs:
            bad=[]
            for offset,value in ((0,0),(len(source)-4,0)):
                data=bytearray(source);data[offset:offset+4]=value.to_bytes(4,'little');bad.append(bytes(data))
            for data in bad:
                buf=C.create_string_buffer(data);assert dll.shader_validator_check(buf,len(data))!=0;negative+=1
            buf=C.create_string_buffer(source);assert dll.shader_validator_check(buf,len(source)-1)!=0;negative+=1
        assert dll.shader_validator_check(None,8)!=0;negative+=1
    finally:dll.shader_validator_close()
    dll.shader_validator_close();buf=C.create_string_buffer(blobs[0]);assert dll.shader_validator_check(buf,len(blobs[0]))!=0;negative+=1
    result=dict(status='PASS',native_positive=2,negative_cases=negative,duplicate_open_rejected=True,close_idempotent=True,game_started=False,gpu_drawn=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',required=True);run(p.parse_args())

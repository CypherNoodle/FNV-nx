"""Native register-bank to reflected Vulkan UBO packing; own inputs only."""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
from types import SimpleNamespace

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from translate_d3d9_pair import translate, digest
from spirv_uniform_layout import reflect
from test_d3d9_spirv import compile_hlsl

U32=C.c_uint32
class Entry(C.Structure):
    _fields_=[(n,U32) for n in ('kind','first','count','offset','stride')]
class Plan(C.Structure):
    _fields_=[('bytes',U32),('count',U32),('entries',C.POINTER(Entry))]
class Banks(C.Structure):
    _fields_=[('bits',C.POINTER(U32)*3),('count',U32*3)]


def assemble(dll,source):
    code,errors=C.c_void_p(),C.c_void_p()
    fn=dll.D3DAssemble
    fn.argtypes=[C.c_void_p,C.c_size_t,C.c_char_p,C.c_void_p,C.c_void_p,U32,C.POINTER(C.c_void_p),C.POINTER(C.c_void_p)]
    fn.restype=C.c_int32
    def method(blob,index,result):
        table=C.cast(blob,C.POINTER(C.POINTER(C.c_void_p))).contents
        return C.WINFUNCTYPE(result,C.c_void_p)(table[index])(blob)
    def payload(blob):return C.string_at(method(blob,3,C.c_void_p),method(blob,4,C.c_size_t))
    hr=fn(source,len(source),b'fnv-own-register-banks',None,None,0,C.byref(code),C.byref(errors))
    try:
        if hr<0:raise RuntimeError(payload(errors).decode(errors='replace') if errors else hex(hr))
        return payload(code)
    finally:
        for blob in (code,errors):
            if blob:method(blob,2,U32)


def test(args):
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    native=Path(__file__).resolve().parents[1]/'native'
    dllpath=out/'uniform_pack.dll'
    command=[*args.cc,'-shared','-O2','-std=c11','-Wall','-Wextra','-Werror',
             '-Wl,--export-all-symbols',str(native/'d3d9_uniform_pack.c'),'-o',str(dllpath)]
    subprocess.run(command,check=True)
    dll=C.CDLL(str(dllpath))
    fn=dll.fnv_d3d9_uniform_pack
    fn.argtypes=[C.POINTER(Plan),C.POINTER(Banks),C.c_void_p,U32];fn.restype=U32
    rng=random.Random(0x55424f)
    storage=[(U32*n)(*[rng.getrandbits(32) for _ in range(n)]) for n in (1024,64,16)]
    storage[0][:8]=[0,0x80000000,0x7fc01234,0xff800000,0x3f800000,0xffffffff,1,0x80000001]
    storage[2][:4]=[0,1,0xffffffff,0x80000000]
    banks=Banks((C.POINTER(U32)*3)(*[C.cast(s,C.POINTER(U32)) for s in storage]),(U32*3)(256,16,16))
    initial=[bytes(s) for s in storage]

    def invoke(entries,size=128,capacity=None,expect=0):
        array=(Entry*len(entries))(*[Entry(**e) for e in entries])
        plan=Plan(size,len(entries),array)
        output=(C.c_ubyte*(size+32))(*([0xa5]*(size+32)))
        before=bytes(output)
        result=fn(C.byref(plan),C.byref(banks),output,size if capacity is None else capacity)
        assert result==expect,(hex(result),entries)
        if expect:
            assert bytes(output)==before
        else:
            expected=bytearray(size)
            for e in entries:
                for i in range(e['count']):
                    start=e['offset']+16*i
                    if e['kind']==2:
                        struct.pack_into('<I',expected,start,int(storage[2][e['first']+i]!=0))
                    else:
                        reg=(e['first']+i)*4
                        expected[start:start+16]=struct.pack('<4I',*storage[e['kind']][reg:reg+4])
            assert bytes(output)[:size]==expected and bytes(output)[size:]==before[size:]
        assert [bytes(s) for s in storage]==initial
        return bytes(output)[:size]

    for _ in range(600):
        kinds=rng.sample(range(3),3);entries=[];offset=0
        for kind in kinds:
            count=rng.randint(1,4);limit=256 if kind==0 else 16
            entries.append(dict(kind=kind,first=rng.randint(0,limit-count),count=count,offset=offset,stride=16))
            offset+=count*16+16  # Deliberate padding must be zero.
        invoke(entries,offset)
    valid=[dict(kind=0,first=0,count=2,offset=0,stride=16),dict(kind=2,first=0,count=4,offset=32,stride=16)]
    invoke(valid)
    negatives=0
    for change in [dict(kind=3),dict(first=256),dict(count=0),dict(count=257),dict(stride=4),dict(offset=1),dict(offset=128),dict(offset=0xfffffff0)]:
        bad=[dict(valid[0],**change),valid[1]]
        invoke(bad,expect=0x8876086c);negatives+=1
    invoke([valid[0],dict(valid[1],offset=16)],expect=0x8876086c);negatives+=1
    invoke(valid,capacity=127,expect=0x8876086c);negatives+=1
    invoke(valid,size=127,expect=0x8876086c);negatives+=1
    array=(Entry*1)(Entry(0,0,1,0,16));plan=Plan(16,1,array)
    assert fn(C.byref(plan),C.byref(banks),storage[0],16)==0x8876086c
    assert bytes(storage[0])==initial[0];negatives+=1
    assert fn(None,C.byref(banks),None,0)==0x8876086c;negatives+=1
    empty=Plan(0,0,None);assert fn(C.byref(empty),C.byref(banks),None,0)==0

    compiler=Path(os.environ['SystemRoot'])/'System32/d3dcompiler_47.dll'
    d3d=C.WinDLL(str(compiler))
    # Explicit D3D9 assembly exposes global INT/BOOL banks. HLSL lowers most
    # integer parameters into c-registers rather than accepting register(i2).
    vs=b'''vs_3_0
def c0, -2, 0, 0, 1
def c1, 4, 0, 0, 0
dcl_position0 v0
dcl_texcoord0 v1
dcl_position0 o0
dcl_texcoord0 o1
mov r0, v0
add r0.x, r0.x, c1.x
rep i2
add r0.x, r0.x, c0.x
endrep
if b3
else
add r0.x, r0.x, c1.x
endif
mov r0.z, c7.z
add r0.z, r0.z, c9.z
mov r0.w, c0.w
mov o0, r0
mov o1, v1
'''
    ps=(b'sampler2D image:register(s0);float4 bias:register(c3);float4 tint:register(c12);'
        b'float4 main(float2 uv:TEXCOORD0):COLOR0{return tex2D(image,uv)*tint+bias;}')
    for stage,source in [('vs',vs),('ps',ps)]:
        (out/(stage+('.asm' if stage=='vs' else '.hlsl'))).write_bytes(source)
        (out/(stage+'.bin')).write_bytes(assemble(d3d,source) if stage=='vs' else compile_hlsl(d3d,source,b'ps_3_0'))
    (out/'attributes.txt').write_text('0 0 1\n5 0 1\n',encoding='ascii')
    translation=translate(SimpleNamespace(vs=out/'vs.bin',ps=out/'ps.bin',attributes=out/'attributes.txt',linker=args.linker,output=out/'pair'))
    plans={s:translation['stages'][s]['uniform_plan'] for s in ('vs','ps')}
    assert {e['first'] for e in plans['vs']['entries'] if e['kind']==0}=={7,9}
    assert {(e['kind'],e['first']) for e in plans['vs']['entries'] if e['kind']!=0}=={(1,2),(2,3)}
    assert {e['first'] for e in plans['ps']['entries']}=={3,12}
    array_vs=(b'float4 palette[4]:register(c20);'
              b'struct V{float2 p:POSITION;float2 uv:TEXCOORD0;};struct O{float4 p:POSITION;float2 uv:TEXCOORD0;};'
              b'O main(V v){O o;o.p=float4(v.p,palette[(int)(v.uv.x*3)].z,1);o.uv=v.uv;return o;}')
    (out/'array-vs.bin').write_bytes(compile_hlsl(d3d,array_vs,b'vs_3_0'))
    array_pair=translate(SimpleNamespace(vs=out/'array-vs.bin',ps=out/'ps.bin',attributes=out/'attributes.txt',
                                       linker=args.linker,output=out/'array-pair'))
    array_plan=array_pair['stages']['vs']['uniform_plan']
    assert array_plan['bytes']==64 and array_plan['entries']==[dict(kind=0,first=20,count=4,offset=0,stride=16)]
    invoke(array_plan['entries'],64)
    # Native packer's exact output is consumed by the Vulkan pixel test.
    packed=[]
    for frame in range(3):
        for stage in ('vs','ps'):
            local=[(U32*n)() for n in (1024,64,16)]
            for i in range(1024):local[0][i]=0x7fc00001  # Unused API slots must not be uploaded.
            def f(reg,values):
                for j,value in enumerate(values):local[0][reg*4+j]=struct.unpack('<I',struct.pack('<f',value))[0]
            if stage=='vs':
                f(7,[0,0,[.75,.25,.9][frame]-.0625,0]);f(9,[0,0,.0625,0])
                local[1][2*4]=2;local[2][3]=0xffffffff
            else:
                tint=[0.,0.,0.,0.];tint[frame]=1.;f(12,tint);f(3,[0,0,0,1])
            b=Banks((C.POINTER(U32)*3)(*[C.cast(s,C.POINTER(U32)) for s in local]),(U32*3)(256,16,16))
            info=plans[stage];array=(Entry*len(info['entries']))(*[Entry(**e) for e in info['entries']])
            p=Plan(info['bytes'],len(array),array);dest=(C.c_ubyte*p.bytes)()
            assert fn(C.byref(p),C.byref(b),dest,p.bytes)==0
            packed.append(bytes(dest))
    uniform_data=b'FNVUBO01'+struct.pack('<2I',plans['vs']['bytes'],plans['ps']['bytes'])+b''.join(packed)
    (out/'uniform-data.bin').write_bytes(uniform_data)
    # Mutated reflection must reject missing/duplicate/unmapped ranges.
    metadata=translation['link']['vertex_uniform_metadata'];module=(out/'pair/vs.spv').read_bytes()
    for bad in [metadata+metadata,metadata[1:],[dict(u,local=True) for u in metadata]]:
        try:reflect(module,bad)
        except ValueError:negatives+=1
        else:raise AssertionError('invalid uniform reflection accepted')
    fixture=out/'gpu-fixture';fixture.mkdir()
    for stage in ('vs','ps'):shutil.copyfile(out/'pair'/(stage+'.spv'),fixture/('normal.'+stage+'.spv'))
    (fixture/'validation.json').write_text(json.dumps({'status':'PASS','game_executed':False,
        'normal':{s:translation['stages'][s]['spirv'] for s in ('vs','ps')},
        'native_packed_uniforms_sha256':digest(uniform_data)},indent=2)+'\n',encoding='utf-8')
    report={'status':'PASS','random_cases':600,'negative_cases':negatives,'plans':plans,'relative_array_plan':array_plan,
        'game_executed':False,'gpu_rendered':False,'native_dll_sha256':digest(dllpath.read_bytes()),
        'input_sha256':{str(p):digest(p.read_bytes()) for p in [Path(__file__),native/'d3d9_uniform_pack.c',
            native/'d3d9_uniform_pack.h',compiler]},'uniform_data_sha256':digest(uniform_data)}
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'status':'PASS','random_cases':600,'negative_cases':negatives,'plans':plans}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--linker',type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    test(p.parse_args())

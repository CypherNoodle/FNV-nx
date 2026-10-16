"""Own SM2/SM3 centroid bit preservation, SPIR-V mapping and rejection tests."""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import shutil
import struct
import sys
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from translate_d3d9_pair import normalize, restore, translate, digest
from inspect_spirv import inspect
from test_d3d9_spirv import VS, PS, compile_hlsl


def test(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    compiler = Path(os.environ['SystemRoot'])/'System32/d3dcompiler_47.dll'
    dll = C.WinDLL(str(compiler))
    original_vs = compile_hlsl(dll, VS, b'vs_3_0')
    original_ps = compile_hlsl(dll, PS.replace(b'float2 uv:', b'centroid float2 uv:'), b'ps_3_0')
    (out/'vs.bin').write_bytes(original_vs)
    (out/'ps.bin').write_bytes(original_ps)
    (out/'attributes.txt').write_text('0 0 1\n5 0 1\n', encoding='ascii')
    fixed, declarations = normalize(original_ps)
    assert len(declarations) == 1 and declarations[0]['usage'] == 5 and declarations[0]['index'] == 0
    before = struct.unpack('<%dI'%(len(original_ps)//4),original_ps)
    after = struct.unpack('<%dI'%(len(fixed)//4),fixed)
    changes = [i for i in range(len(before)) if before[i] != after[i]]
    assert changes == [declarations[0]['token_word']]
    assert before[changes[0]] ^ after[changes[0]] == 0x00400000
    assert normalize(original_vs) == (original_vs, [])
    proof = translate(SimpleNamespace(vs=out/'vs.bin',ps=out/'ps.bin',attributes=out/'attributes.txt',
                                     linker=args.linker,output=out/'pair'))
    multi_vs = VS.replace(b'float2 uv:TEXCOORD0;};O main',b'float2 uv:TEXCOORD0;float2 uv1:TEXCOORD1;};O main')
    multi_vs = multi_vs.replace(b'o.uv=v.uv;',b'o.uv=v.uv;o.uv1=v.uv;')
    multi_ps = (b'sampler2D image:register(s0);float4 tint:register(c0);'
                b'float4 main(centroid float2 a:TEXCOORD0,centroid float2 b:TEXCOORD1):COLOR0'
                b'{return tex2D(image,(a+b)*0.5)*tint;}')
    (out/'multi-vs.bin').write_bytes(compile_hlsl(dll,multi_vs,b'vs_3_0'))
    (out/'multi-ps.bin').write_bytes(compile_hlsl(dll,multi_ps,b'ps_3_0'))
    multi = translate(SimpleNamespace(vs=out/'multi-vs.bin',ps=out/'multi-ps.bin',attributes=out/'attributes.txt',
                                     linker=args.linker,output=out/'multi-pair'))
    assert len(multi['stages']['ps']['centroid_ids']) == 2
    pixel = (out/'pair/ps.spv').read_bytes()
    plain = (out/'pair/ps.linked.spv').read_bytes()
    assert len(pixel) == len(plain)+12
    assert sum(v.get('centroid',False) for v in inspect(pixel)['interfaces']) == 1
    assert not any(v.get('centroid') for v in inspect(plain)['interfaces'])
    assert restore(plain,[])[0] == plain
    failures = 0

    def reject(fn, *values):
        nonlocal failures
        try:
            fn(*values)
        except ValueError:
            failures += 1
            return
        raise AssertionError('invalid centroid input accepted')

    reject(restore,pixel,declarations)  # duplicate decoration
    reject(restore,(out/'pair/vs.spv').read_bytes(),declarations)
    reject(restore,plain,[dict(declarations[0],spirv_name='absent')])
    reject(restore,plain,declarations*2)
    for data in [original_ps[:7],original_ps[:-4],original_ps+b'\0\0\0\0']:
        reject(normalize,data)
    # Minimal SM2.1 DCL t1 centroid plus END; normalization-only, not a valid complete program.
    sm2 = struct.pack('<5I',0xffff0201,0x0200001f,0x80000000,0xb04f0001,0xffff)
    normalized, d = normalize(sm2)
    assert d[0]['usage'] == 5 and d[0]['index'] == 1 and d[0]['spirv_name'] == 'ps_t1'
    assert struct.unpack('<5I',normalized)[3] == 0xb00f0001
    # Skip fake DCL-like words inside comments instead of changing arbitrary data.
    comment = struct.pack('<6I',0xffff0300,0x0003fffe,0x0200001f,0x80000005,0x904f0000,0xffff)
    assert normalize(comment) == (comment,[])
    for version,dest in [(0xfffe0300,0x904f0000),(0xffff0300,0xb04f0000),(0xffff0300,0x904f2000)]:
        reject(normalize,struct.pack('<5I',version,0x0200001f,0x80000005,dest,0xffff))
    fixture = out/'gpu-fixture'
    fixture.mkdir()
    for stage in ('vs','ps'):
        shutil.copyfile(out/'multi-pair'/(stage+'.spv'),fixture/('normal.'+stage+'.spv'))
    (fixture/'validation.json').write_text(json.dumps({'status':'PASS','game_executed':False,
        'normal':{stage:multi['stages'][stage]['spirv'] for stage in ('vs','ps')},
        'centroid_translation':digest((out/'multi-pair/translation.json').read_bytes())},indent=2)+'\n',encoding='utf-8')
    report = {'status':'PASS','single_bit_changes':changes,'centroid_inputs_restored':1,
              'multi_input_centroid_restored':2,'negative_cases':failures,'compiler_sha256':digest(compiler.read_bytes()),
              'test_sha256':digest(Path(__file__).read_bytes()),'gpu_rendered':False,
              'msaa_coverage_validated':False,'game_executed':False}
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--linker',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    test(p.parse_args())

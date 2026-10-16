"""Compile our own HLSL with Windows, then link it for Vulkan offline.

No Fallout execution and no GPU rendering. No external shader source edits.
The structural checks below do not replace spirv-val or a rendered comparison.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_spirv import inspect

VS = (b'float4 depth:register(c0);'
      b'struct V{float2 p:POSITION;float2 uv:TEXCOORD0;};'
      b'struct O{float4 p:POSITION;float2 uv:TEXCOORD0;};'
      b'O main(V v){O o;o.p=float4(v.p,depth.z,1);o.uv=v.uv;return o;}')
PS = (b'sampler2D image:register(s0);float4 tint:register(c0);'
      b'float4 main(float2 uv:TEXCOORD0):COLOR0{return tex2D(image,uv)*tint;}')


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def compile_hlsl(dll, source, profile):
    code, errors = C.c_void_p(), C.c_void_p()
    fn = dll.D3DCompile
    fn.argtypes = [C.c_void_p, C.c_size_t, C.c_char_p, C.c_void_p, C.c_void_p,
                   C.c_char_p, C.c_char_p, C.c_uint, C.c_uint,
                   C.POINTER(C.c_void_p), C.POINTER(C.c_void_p)]
    fn.restype = C.c_int32

    def method(blob, slot, result):
        vtable = C.cast(blob, C.POINTER(C.POINTER(C.c_void_p))).contents
        return C.WINFUNCTYPE(result, C.c_void_p)(vtable[slot])(blob)

    def payload(blob):
        return C.string_at(method(blob, 3, C.c_void_p), method(blob, 4, C.c_size_t))

    hr = fn(source, len(source), b'fnv-own-draw', None, None, b'main', profile,
            1 << 15, 0, C.byref(code), C.byref(errors))
    try:
        if hr < 0:
            raise RuntimeError(payload(errors).decode(errors='replace') if errors else hex(hr))
        return payload(code)
    finally:
        for blob in (code, errors):
            if blob:
                method(blob, 2, C.c_uint)


def test(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    compiler = Path(os.environ['SystemRoot']) / 'System32/d3dcompiler_47.dll'
    dll = C.WinDLL(str(compiler))
    for name, source, profile in [('vs', VS, b'vs_3_0'), ('ps', PS, b'ps_3_0')]:
        (out / (name+'.hlsl')).write_bytes(source)
        (out / (name+'.bin')).write_bytes(compile_hlsl(dll, source, profile))
    # Public MojoShader enums: POSITION=0, TEXCOORD=5, VECTOR2=1.
    (out / 'attributes.txt').write_text('0 0 1\n5 0 1\n', encoding='ascii')

    def run(label, vs='vs.bin', ps='ps.bin', attributes='attributes.txt', expected=0):
        targets = [out / (label+'.vs.spv'), out / (label+'.ps.spv')]
        command = [str(args.linker.resolve()), str(out/vs), str(out/ps), str(out/attributes),
                   *map(str, targets)]
        r = subprocess.run(command, capture_output=True, text=True)
        if r.returncode != expected:
            raise AssertionError((label, r.returncode, r.stderr))
        if expected:
            assert not any(p.exists() for p in targets), label
            return {'exit': r.returncode, 'message': r.stderr.strip()}
        return {'link': json.loads(r.stdout),
                'vs': inspect(targets[0].read_bytes()), 'ps': inspect(targets[1].read_bytes())}

    normal = run('normal')
    assert normal['vs']['entry']['model'] == 0 and normal['ps']['entry']['model'] == 4
    assert {(v['set'], v['binding']) for v in normal['vs']['resources']} == {(1, 0)}
    assert {(v['set'], v['binding']) for v in normal['ps']['resources']} == {(2, 0), (3, 0)}
    vout = {v['location'] for v in normal['vs']['interfaces'] if v['storage'] == 3 and 'location' in v}
    pin = {v['location'] for v in normal['ps']['interfaces'] if v['storage'] == 1 and 'location' in v}
    assert pin and pin <= vout and 0xffffffff not in (pin | vout)
    negatives = {}
    (out / 'duplicate.txt').write_text('0 0 1\n0 0 1\n', encoding='ascii')
    negatives['duplicate'] = run('duplicate', attributes='duplicate.txt', expected=3)
    (out / 'missing.txt').write_text('0 0 1\n', encoding='ascii')
    negatives['missing'] = run('missing', attributes='missing.txt', expected=4)
    (out / 'range.txt').write_text('999 0 1\n', encoding='ascii')
    negatives['range'] = run('range', attributes='range.txt', expected=3)
    (out / 'bad.bin').write_bytes(b'not bytecode')
    negatives['bad_bytecode'] = run('bad', vs='bad.bin', expected=4)
    negatives['wrong_stage'] = run('wrong', vs='ps.bin', expected=4)
    (out/'mismatch.bin').write_bytes(compile_hlsl(dll, PS.replace(b'TEXCOORD0',b'TEXCOORD1'), b'ps_3_0'))
    negatives['interstage_mismatch'] = run('mismatch', ps='mismatch.bin', expected=4)
    # Opening the second output must not leave a first-stage partial file or
    # overwrite an accepted artifact. Also retry with both files already there.
    (out/'collision.ps.spv').write_bytes(b'preserve')
    command = [str(args.linker.resolve()), str(out/'vs.bin'), str(out/'ps.bin'), str(out/'attributes.txt'),
               str(out/'collision.vs.spv'), str(out/'collision.ps.spv')]
    collision = subprocess.run(command, capture_output=True)
    assert collision.returncode == 5 and not (out/'collision.vs.spv').exists()
    assert (out/'collision.ps.spv').read_bytes() == b'preserve'
    negatives['second_output_exists'] = {'exit': collision.returncode}
    accepted = [sha(out/'normal.vs.spv'), sha(out/'normal.ps.spv')]
    command[-2:] = [str(out/'normal.vs.spv'), str(out/'normal.ps.spv')]
    exists = subprocess.run(command, capture_output=True)
    assert exists.returncode == 5 and accepted == [sha(out/'normal.vs.spv'), sha(out/'normal.ps.spv')]
    negatives['accepted_outputs_exist'] = {'exit': exists.returncode}
    raw = (out / 'normal.vs.spv').read_bytes()
    corruptions = [raw[:19], raw+b'\0\0\0\0', raw[:-2], b'BAD!'+raw[4:],
                   raw[:20]+struct.pack('<I', 0)+raw[24:]]
    for data in corruptions:
        try:
            inspect(data)
        except ValueError:
            continue
        raise AssertionError('malformed module accepted')
    report = {'status': 'PASS', 'normal': normal, 'negative_cases': negatives,
              'malformed_module_cases': len(corruptions), 'game_executed': False,
              'gpu_rendered': False, 'switch_hardware_tested': False,
              'semantic_validation': False,
              'input_sha256': {'compiler': sha(compiler), 'linker': sha(args.linker),
                               'test': sha(Path(__file__)),
                               'inspector': sha(Path(inspect.__code__.co_filename)),
                               'vs': sha(out/'vs.bin'), 'ps': sha(out/'ps.bin')}}
    (out/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'status': 'PASS', 'vertex_bytes': normal['vs']['bytes'],
                      'pixel_bytes': normal['ps']['bytes'], 'negative_cases': len(negatives),
                      'gpu_rendered': False}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--linker', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    test(p.parse_args())

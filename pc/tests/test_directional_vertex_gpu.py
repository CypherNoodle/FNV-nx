"""Compile our bounded directional vertex prototype offline and verify actual Vulkan pixels."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
from test_d3d9_spirv import compile_hlsl
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from translate_d3d9_pair import translate


def run(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    compiler=Path(os.environ['SystemRoot'])/'System32/d3dcompiler_47.dll'
    dll=C.WinDLL(str(compiler))
    shader_sources=([pc/'shaders/fixed_directional_textured.vs.hlsl',pc/'shaders/fixed_directional_modulate.ps.hlsl'] if a.textured else [pc/'shaders/fixed_directional.vs.hlsl',pc/'tests/fixed_directional_color.ps.hlsl'])
    native=['d3d9_vk_backend.c','d3d9_geometry.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_vk_state.c','d3d9_constants.c']
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    inputs=[Path(__file__).resolve(),Path(compile_hlsl.__code__.co_filename),*shader_sources,compiler,Path(a.cc[0]).resolve(),a.linker.resolve(),pc/'tests/directional_vertex_gpu_host.c',pc/'tests/directional_vertex_oracle.h',
            *[p for p in (pc/'native').iterdir() if p.suffix in ('.h','.inc')],*[pc/'native'/n for n in native],
            *a.headers.resolve().rglob('*.h'),*[(pc/'tools'/n) for n in ('translate_d3d9_pair.py','inspect_spirv.py','spirv_uniform_layout.py')]]
    frozen={str(p):sha(p) for p in inputs}
    for stage,source in zip(('vs','ps'),shader_sources):
        (out/(stage+'.bin')).write_bytes(compile_hlsl(dll,source.read_bytes(),(stage+'_3_0').encode()))
    (out/'attributes.txt').write_text('0 0 2\n3 0 2\n'+('5 0 1\n' if a.textured else ''),encoding='ascii')
    report=translate(SimpleNamespace(vs=out/'vs.bin',ps=out/'ps.bin',attributes=out/'attributes.txt',linker=a.linker,output=out/'pair'))
    plans={s:report['stages'][s]['uniform_plan'] for s in ('vs','ps')}
    assert plans['vs']['bytes']==160 and not plans['ps']['bytes']
    registers={i for e in plans['vs']['entries'] for i in range(e['first'],e['first']+e['count'])}
    assert registers==set(range(10)) and all(e['kind']==0 for e in plans['vs']['entries'])
    header=['#define FNV_DIRECTIONAL_TEXTURED 1'] if a.textured else []
    for stage,name in (('vs','vertex'),('ps','pixel')):
        plan=plans[stage];pointer='NULL'
        if plan['entries']:
            header.append('static const struct fnv_uniform_entry '+name+'_entries[]={'+','.join('{'+','.join(str(e[k]) for k in ('kind','first','count','offset','stride'))+'}' for e in plan['entries'])+'};')
            pointer=name+'_entries'
        header.append('static const struct fnv_uniform_plan '+name+'_plan={'+str(plan['bytes'])+','+str(len(plan['entries']))+','+pointer+'};')
    interface=report['stages']['vs']['spirv']['interfaces'];locations={v['name']:v['location'] for v in interface if v['storage']==1 and 'location' in v}
    header.append('static const struct fnv_vk_vertex_input vertex_inputs[]={{0,0,%d},{3,0,%d}};'%(locations['vs_v0'],locations['vs_v1']))
    if a.textured:
        header[-1]='static const struct fnv_vk_vertex_input vertex_inputs[]={{0,0,%d},{3,0,%d},{5,0,%d}};'%(locations['vs_v0'],locations['vs_v1'],locations['vs_v2'])
    (out/'directional_vertex_fixture.h').write_text('\n'.join(header)+'\n',encoding='ascii')
    env=os.environ.copy();env['PATH']=str(Path(a.cc[0]).resolve().parent)+os.pathsep+env['PATH']
    command=[*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-fno-strict-aliasing','-ffp-contract=off',
             '-I'+str(pc/'native'),'-I'+str(out),'-I'+str(a.headers.resolve()),str(pc/'tests/directional_vertex_gpu_host.c'),
             *[str(pc/'native'/n) for n in native],'-ld3d9','-luser32','-o',str(out/'gpu-test.exe')]
    r=subprocess.run(command,capture_output=True,text=True,env=env,timeout=120);(out/'compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr)
    args=[str(out/'gpu-test.exe'),str(out/'vs.bin'),str(out/'ps.bin'),str(out/'pair/vs.spv'),str(out/'pair/ps.spv')]
    r=subprocess.run(args,capture_output=True,text=True,env=env,timeout=60);(out/'normal.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    controls={}
    for mode in (('wrong-normal','wrong-direction','wrong-uv','wrong-texture-alpha') if a.textured else ('wrong-normal','wrong-direction')):
        bad=subprocess.run([*args,mode],capture_output=True,text=True,env=env,timeout=60)
        (out/(mode+'.log')).write_text(bad.stdout+bad.stderr)
        assert bad.returncode and 'directional pixel mismatch' in bad.stderr
        controls[mode]=True
    assert all(sha(Path(p))==h for p,h in frozen.items())
    result=dict(status='PASS',textured=a.textured,pixel_checks=7168,negative_controls=controls,source_sha256=frozen,
                compiler_command=command,output_sha256={str(p.relative_to(out)):sha(p) for p in [out/'vs.bin',out/'ps.bin',out/'pair/vs.spv',out/'pair/ps.spv',out/'directional_vertex_fixture.h',out/'gpu-test.exe',out/'pair/translation.json']},translated_callers=False,automatic_fixed_pipeline=False,game_frame=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS directional vertex GPU7168 pixels; all %d negative controls detected'%len(controls))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for n in ('output','linker','headers'):parser.add_argument('--'+n,type=Path,required=True)
    parser.add_argument('--textured',action='store_true')
    parser.add_argument('--cc',nargs='+',required=True);run(parser.parse_args())

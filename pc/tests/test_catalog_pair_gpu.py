"""Compare an explicitly selected texture-copy shader pair on D3D9 and Vulkan.

Bounded fixture: POSITION float4, TEXCOORD0 float2, sampler2D s0, optional float banks.
Selection from an original package does not prove the game binds that pair.
All extracted bytecode and generated SPIR-V remain in private output.
"""
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
from shader_packages import parse_package
from translate_d3d9_pair import translate,normalize

def run(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    cc=Path(a.cc).resolve();linker=a.linker.resolve();env=os.environ.copy();env['PATH']=str(cc.parent)+os.pathsep+str(linker.parent)+os.pathsep+env['PATH']
    native=['d3d9_vk_backend.c','d3d9_vk_prepared.c','d3d9_geometry.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_vk_state.c','d3d9_constants.c']
    sources=[Path(__file__).resolve(),pc/'tests/catalog_pair_gpu_host.c',pc/'tests/catalog_pair_oracle.h',pc/'tests/catalog_mip_fixture.h',Path(compile_hlsl.__code__.co_filename),linker,cc,
             *[pc/'native'/n for n in native],*[p for p in (pc/'native').iterdir() if p.suffix in ('.h','.inc')],
             *[(pc/'tools'/n) for n in ('shader_packages.py','translate_d3d9_pair.py','inspect_spirv.py','spirv_uniform_layout.py')]]
    frozen={str(p):sha(p) for p in sources}
    if a.package:
        if not a.package_sha256 or not a.vertex_sha256 or not a.pixel_sha256:raise ValueError('explicit package and both shader hashes required')
        assert sha(a.package)==a.package_sha256,'package identity mismatch';frozen[str(a.package.resolve())]=sha(a.package)
        records=parse_package(a.package.read_bytes())
        for stage,key in (('vs',a.vertex_sha256),('ps',a.pixel_sha256)):
            blobs={r['code'] for r in records if r['sha256']==key and r['stage']==('vertex' if stage=='vs' else 'pixel')}
            if len(blobs)!=1:raise ValueError('selected shader missing or ambiguous')
            (out/(stage+'.bin')).write_bytes(blobs.pop())
    else:
        dllpath=Path(os.environ['SystemRoot'])/'System32/d3dcompiler_47.dll';frozen[str(dllpath)]=sha(dllpath);dll=C.WinDLL(str(dllpath))
        own_vs=b'struct V{float4 p:POSITION;float2 uv:TEXCOORD0;};V main(V v){return v;}'
        own_ps=b'sampler2D image:register(s0);float4 main(float2 uv:TEXCOORD0):COLOR0{return tex2D(image,uv);}'
        if a.constants:
            own_vs=b'row_major float4x4 transform:register(c0);struct V{float4 p:POSITION;float2 uv:TEXCOORD0;};V main(V v){v.p=mul(v.p,transform);return v;}'
            own_ps=b'float4 tint:register(c5);sampler2D image:register(s0);float4 main(float2 uv:TEXCOORD0):COLOR0{return tex2D(image,uv)*tint;}'
        for stage,source in (('vs',own_vs),('ps',own_ps)):(out/(stage+'.bin')).write_bytes(compile_hlsl(dll,source,(stage+'_3_0').encode()))
    descriptors={}
    for stage in ('vs','ps'):
        normalized,_=normalize((out/(stage+'.bin')).read_bytes());path=out/(stage+'.describe.bin');path.write_bytes(normalized)
        p=subprocess.run([str(linker),'--describe',str(path)],capture_output=True,text=True,env=env,timeout=30)
        assert not p.returncode,p.stderr;descriptors[stage]=json.loads(p.stdout)
    v,p=descriptors['vs'],descriptors['ps']
    if v['stage']!=2 or v['inputs']!=[dict(usage=0,index=0),dict(usage=5,index=0)] or v['samplers']:raise ValueError('vertex shader outside fixture contract')
    if p['stage']!=1 or p['inputs']!=[dict(usage=5,index=0)] or p['samplers']!=[dict(index=0,type=0,texbem=False)]:raise ValueError('pixel shader outside fixture contract')
    for d in (v,p):
        active=[u for u in d['uniforms'] if not u['local']]
        if not a.constants and active:raise ValueError('uniforms require explicit --constants fixture')
        if any(u['kind']!=0 for u in active):raise ValueError('only float banks supported by this fixture')
        if a.constants and not active:raise ValueError('constant fixture requires active uniforms in both stages')
    (out/'attributes.txt').write_text('0 0 3\n5 0 1\n')
    pair=translate(SimpleNamespace(vs=out/'vs.bin',ps=out/'ps.bin',attributes=out/'attributes.txt',linker=linker,output=out/'pair'))
    plans={s:pair['stages'][s]['uniform_plan'] for s in ('vs','ps')}
    locations={i['name']:i['location'] for i in pair['stages']['vs']['spirv']['interfaces'] if i['storage']==1 and 'location' in i}
    assert set(locations)=={'vs_v0','vs_v1'}
    header=['#define FNV_CATALOG_CONSTANTS 1'] if a.constants else []
    if a.mip_clamp:header.append('#define FNV_CATALOG_MIPS 1')
    for stage,name in (('vs','vertex'),('ps','pixel')):
        plan=plans[stage];pointer='NULL'
        if plan['entries']:
            header.append('static const struct fnv_uniform_entry '+name+'_entries[]={'+','.join('{'+','.join(str(e[k]) for k in ('kind','first','count','offset','stride'))+'}' for e in plan['entries'])+'};')
            pointer=name+'_entries'
        header.append('static const struct fnv_uniform_plan '+name+'_plan={'+str(plan['bytes'])+','+str(len(plan['entries']))+','+pointer+'};')
    header.append('static const struct fnv_vk_vertex_input vertex_inputs[]={{0,0,%d},{5,0,%d}};'%(locations['vs_v0'],locations['vs_v1']))
    (out/'catalog_pair_fixture.h').write_text('\n'.join(header)+'\n')
    command=[str(cc),'-std=c11','-O1','-Wall','-Wextra','-Werror','-fno-strict-aliasing','-ffp-contract=off','-I'+str(pc/'native'),'-I'+str(out),'-I'+str(a.headers.resolve()),str(pc/'tests/catalog_pair_gpu_host.c'),*[str(pc/'native'/n) for n in native],'-ld3d9','-luser32','-o',str(out/'gpu-test.exe')]
    b=subprocess.run(command,capture_output=True,text=True,env=env,timeout=120);(out/'compiler.log').write_text(b.stdout+b.stderr);assert not b.returncode,b.stderr
    args=[str(out/'gpu-test.exe'),str(out/'vs.bin'),str(out/'ps.bin'),str(out/'pair/vs.spv'),str(out/'pair/ps.spv')]
    p=subprocess.run(args,capture_output=True,text=True,env=env,timeout=60);(out/'normal.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stdout+p.stderr
    assert 'PASS catalog pair 6144 pixels' in p.stdout
    controls=('wrong-mip-clamp','wrong-mip-filter') if a.mip_clamp else ('wrong-uv','wrong-texture','wrong-vs-constants','wrong-ps-constants','missing-pixel-center') if a.constants else ('wrong-uv','wrong-texture')
    for mode in controls:
        bad=subprocess.run([*args,mode],capture_output=True,text=True,env=env,timeout=60);(out/(mode+'.log')).write_text(bad.stdout+bad.stderr)
        assert bad.returncode and 'catalog pixel mismatch' in bad.stderr
    assert all(sha(Path(n))==h for n,h in frozen.items())
    report=dict(status='PASS',pixel_checks=6144,negative_controls=len(controls),mip_clamp=a.mip_clamp,float_constants=a.constants,original_package_used=bool(a.package),runtime_pair_observed=False,game_frame=False,switch_hardware_tested=False,source_sha256=frozen,
                shader_sha256={s:sha(out/(s+'.bin')) for s in ('vs','ps')},spirv_sha256={s:sha(out/'pair'/(s+'.spv')) for s in ('vs','ps')},descriptors=descriptors)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ('status','pixel_checks','negative_controls','original_package_used','game_frame')}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('output','linker','headers'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',required=True);p.add_argument('--package',type=Path);p.add_argument('--constants',action='store_true')
    p.add_argument('--mip-clamp',action='store_true')
    for n in ('package-sha256','vertex-sha256','pixel-sha256'):p.add_argument('--'+n)
    run(p.parse_args())

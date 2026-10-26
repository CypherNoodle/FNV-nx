"""Observe Windows D3D9 texture dimension behavior using own silent shaders."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import subprocess
from test_d3d9_spirv import compile_hlsl

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',nargs='+',required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);host=Path(__file__).with_name('d3d9_sampler_type_oracle.c');sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    compiler=Path(os.environ['SystemRoot'])/'System32/d3dcompiler_47.dll';dll=C.WinDLL(str(compiler))
    frozen={str(path):sha(path) for path in (Path(__file__),host,compiler,Path(compile_hlsl.__code__.co_filename))}
    shaders=[('vs',b'float4 main(float2 p:POSITION):POSITION{return float4(p,0,1);}',b'vs_3_0'),
             ('cube',b'samplerCUBE image:register(s0);float4 direction:register(c0);float4 main():COLOR0{return texCUBE(image,direction.xyz);}',b'ps_3_0'),
             ('flat',b'sampler2D image:register(s0);float4 direction:register(c0);float4 main():COLOR0{return tex2D(image,direction.xy);}',b'ps_3_0')]
    for name,source,profile in shaders:(out/(name+'.bin')).write_bytes(compile_hlsl(dll,source,profile))
    env=os.environ.copy();env['PATH']=str(Path(a.cc[0]).resolve().parent)+os.pathsep+env['PATH'];exe=out/'oracle.exe'
    cmd=[*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror',str(host),'-ld3d9','-o',str(exe)]
    built=subprocess.run(cmd,capture_output=True,text=True,env=env);(out/'build.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise RuntimeError(built.stdout+built.stderr)
    run=subprocess.run([str(exe),*[str(out/(name+'.bin')) for name,_,_ in shaders]],capture_output=True,text=True,env=env,timeout=30);(out/'run.log').write_text(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError(run.stdout+run.stderr)
    result=json.loads(run.stdout);cases={(x['shader'],x['texture']):x for x in result['cases']}
    assert cases['cube','cube']['pixel_argb']=='ff00ff00' and cases['2d','2d']['pixel_argb']=='ffff0000'
    assert all(sha(Path(path))==value for path,value in frozen.items())
    result.update(status='PASS',source_sha256=frozen,exe_sha256=sha(exe),windows_gpu_oracle=True,switch_tested=False,game_executed=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))

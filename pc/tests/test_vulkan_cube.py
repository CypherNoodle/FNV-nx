"""Compile own D3D9 cube shaders and verify six-axis lookup on real Vulkan."""
import argparse,ctypes as C,hashlib,json,os,subprocess
from pathlib import Path
from test_d3d9_spirv import compile_hlsl
from inspect_spirv import inspect

def run(a):
 pc=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 sources=[pc/'native'/n for n in ('d3d9_vk_backend.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_geometry.c','d3d9_vk_state.c')]
 host=pc/'tests/vk_cube_sampling_host.c';compiler=Path(os.environ['SystemRoot'])/'System32/d3dcompiler_47.dll'
 inputs=[Path(__file__),host,pc/'tests/vk_sampler_shapes_host.c',*sources,*sorted((pc/'native').glob('*.h')),*sorted((pc/'native').glob('*.inc')),a.linker.resolve(),compiler,Path(compile_hlsl.__code__.co_filename),Path(inspect.__code__.co_filename)]
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();frozen={str(p):sha(p) for p in inputs}
 dll=C.WinDLL(str(compiler));vs=b'float4 main(float2 p:POSITION):POSITION{return float4(p,0,1);}'
 ps=b'samplerCUBE image:register(s0);float4 direction:register(c0);float4 main():COLOR0{return texCUBE(image,direction.xyz);}'
 flat=b'sampler2D image:register(s0);float4 direction:register(c0);float4 main():COLOR0{return tex2D(image,direction.xy);}'
 for stage,source,profile in [('vs',vs,b'vs_3_0'),('ps',ps,b'ps_3_0')]:
  (out/(stage+'.hlsl')).write_bytes(source);(out/(stage+'.bin')).write_bytes(compile_hlsl(dll,source,profile))
 (out/'attributes.txt').write_text('0 0 1\n')
 cmd=[str(a.linker.resolve()),str(out/'vs.bin'),str(out/'ps.bin'),str(out/'attributes.txt'),str(out/'vs.spv'),str(out/'ps.spv')]
 r=subprocess.run(cmd,capture_output=True,text=True);(out/'link.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
 linked=json.loads(r.stdout);modules={n:inspect((out/(n+'.spv')).read_bytes()) for n in ('vs','ps')}
 (out/'flat.bin').write_bytes(compile_hlsl(dll,flat,b'ps_3_0'))
 flat_cmd=[str(a.linker.resolve()),str(out/'vs.bin'),str(out/'flat.bin'),str(out/'attributes.txt'),str(out/'flat-vs.spv'),str(out/'flat.spv')]
 r=subprocess.run(flat_cmd,capture_output=True,text=True);(out/'flat-link.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
 assert (out/'flat-vs.spv').read_bytes()==(out/'vs.spv').read_bytes(),'Different vertex payload needs an explicit variant'
 env=os.environ.copy();env['PATH']=str(Path(a.cc[0]).resolve().parent)+os.pathsep+env['PATH']
 cmd=[*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-I'+str(pc/'native'),'-I'+str(a.headers.resolve()),str(host),*map(str,sources),'-o',str(out/'cube-test.exe')]
 r=subprocess.run(cmd,capture_output=True,text=True,env=env);(out/'build.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
 shape_cmd=[*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-I'+str(pc/'native'),str(pc/'tests/vk_sampler_shapes_host.c'),'-o',str(out/'shape-test.exe')]
 r=subprocess.run(shape_cmd,capture_output=True,text=True,env=env);(out/'shape-build.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
 r=subprocess.run([str(out/'shape-test.exe')],capture_output=True,text=True,env=env,timeout=10);(out/'shape-test.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr
 shape_result=json.loads(r.stdout)
 r=subprocess.run([str(out/'cube-test.exe'),*[str(out/n) for n in ('vs.bin','ps.bin','vs.spv','ps.spv','flat.bin','flat.spv')]],capture_output=True,text=True,env=env,timeout=60);(out/'test.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stdout+r.stderr
 result=json.loads(r.stdout.strip().splitlines()[-1]);assert result['status']=='PASS' and result['cube_axis_and_null_pixels']==2816
 assert all(sha(Path(p))==h for p,h in frozen.items()),'Inputs changed'
 result.update(shape_tests=shape_result,source_sha256=frozen,link=linked,modules=modules,game_started=False)
 (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k not in ('source_sha256','link','modules')}))
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('output','headers','linker'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

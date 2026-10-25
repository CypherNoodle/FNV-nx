"""Real Vulkan smoke and regression checks for deferred texture transfers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

def run(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    sources=[pc/'native'/name for name in ('d3d9_vk_backend.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_geometry.c','d3d9_vk_state.c')]
    tests=[pc/'tests'/name for name in ('vk_texture_lazy_smoke.c','vk_texture_lazy_regression.c')]
    inputs=[*sources,*tests,pc/'tests/vk_cube_texture_fixture.h',Path(__file__).resolve(),*sorted((pc/'native').glob('*.h')),*sorted((pc/'native').glob('*.inc')),*sorted(a.headers.resolve().rglob('*.h'))]
    def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
    frozen={str(p):sha(p) for p in inputs};results={}
    for test in tests:
        exe=out/(test.stem+('.exe' if os.name=='nt' else ''))
        command=[*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-I'+str(pc/'native'),'-I'+str(a.headers.resolve()),str(test),*[str(p) for p in sources],'-o',str(exe)]
        if os.name!='nt':command+=['-lvulkan','-lpthread','-lm']
        built=subprocess.run(command,capture_output=True,text=True);(out/(test.stem+'-build.log')).write_text(built.stdout+built.stderr)
        if built.returncode:raise RuntimeError(built.stderr)
        result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60);(out/(test.stem+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(f'{result.returncode}: '+result.stdout+result.stderr)
        assert 'PASS' in result.stdout;results[test.stem]=dict(exe_sha256=sha(exe),output=result.stdout)
    assert frozen=={str(p):sha(p) for p in inputs}
    report=dict(status='PASS',results=results,source_sha256=frozen,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('headers','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

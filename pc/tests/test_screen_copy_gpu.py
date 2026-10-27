"""Test an explicit XYZ/UV copy-pair contract with a synthetic quadrant image."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from emit_prepared_program import emit

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('fixture','output','headers'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);a=p.parse_args();pc=Path(__file__).resolve().parents[1]
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);report=emit(a.fixture.resolve(),[[0,0,0],[5,0,1]],1,out/'program.h')
    source=pc/'tests/vk_screen_copy_host.c';native=[pc/'native'/n for n in ('d3d9_vk_backend.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_geometry.c','d3d9_vk_state.c')]
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();inputs=[Path(__file__),source,*native,*sorted((pc/'native').glob('*.h')),*sorted((pc/'native').glob('*.inc'))]
    frozen={str(p):sha(p) for p in inputs};cmd=[*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-I'+str(pc/'native'),'-I'+str(a.headers.resolve()),'-DFNV_SCREEN_COPY_HEADER="'+str(out/'program.h').replace('\\','/')+'"',str(source),*map(str,native),'-o',str(out/'copy-test.exe')]
    env=os.environ.copy();env['PATH']=str(Path(a.cc[0]).resolve().parent)+os.pathsep+env['PATH'];r=subprocess.run(cmd,capture_output=True,text=True,env=env);(out/'build.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    r=subprocess.run([str(out/'copy-test.exe')],capture_output=True,text=True,env=env,timeout=30);(out/'run.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    result=json.loads(r.stdout)
    negative=subprocess.run([str(out/'copy-test.exe'),'--wrong-uv'],capture_output=True,text=True,env=env,timeout=30)
    (out/'negative.log').write_text(negative.stdout+negative.stderr)
    if negative.returncode!=1 or 'pixel ' not in negative.stderr or 'FAIL line ' not in negative.stderr:
        raise RuntimeError('Wrong-UV negative control did not fail the pixel comparison')
    result['wrong_uv_rejected']=True
    assert all(sha(Path(p))==h for p,h in frozen.items())
    result.update(source_sha256=frozen,prepared=report,exe_sha256=sha(out/'copy-test.exe'))
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k not in ('source_sha256','prepared')}))

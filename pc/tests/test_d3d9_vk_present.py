"""Compile/run actual Windows Vulkan WSI; no D3D9 Windows or game code."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

def run(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve()
    if out.exists():raise ValueError('Use a fresh private output directory')
    out.mkdir(parents=True)
    sources=[pc/'native'/n for n in ('d3d9_vk_backend.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_geometry.c','d3d9_vk_state.c')]+[pc/'tests/d3d9_vk_present_host.c']
    inputs=[*sources,Path(__file__).resolve(),pc/'tests/vk_backend_fault_proxy.h',*sorted((pc/'native').glob('*.h')),*sorted((pc/'native').glob('d3d9_vk_*.inc')),*sorted((a.headers/'vulkan').glob('*.h'))]
    def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
    frozen={str(p):sha(p) for p in inputs};exe=out/'wsi-test.exe'
    command=[*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-I'+str(pc/'native'),'-I'+str(a.headers.resolve()),*[str(p) for p in sources],'-luser32','-o',str(exe)]
    r=subprocess.run(command,capture_output=True,text=True);(out/'compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr)
    r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60);(out/'run.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(f'exit {r.returncode}: {r.stdout}{r.stderr}')
    result=json.loads(r.stdout.strip().splitlines()[-1]);assert result['status']=='PASS' and result['actual_wsi_presents']==18 and result['copy_commands']==22 and result['runtime_faults']==7
    assert {str(p):sha(p) for p in inputs}==frozen
    result.update(source_sha256=frozen,exe_sha256=sha(exe),native_engine_started=False,desktop_d3d9_used=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--headers',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

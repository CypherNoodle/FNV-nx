"""Build/run pure native D3D9 -> Vulkan depth/sampler translation checks."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def run(args):
    pc=Path(__file__).resolve().parents[1];out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    headers=args.headers.resolve();source=pc/'native/d3d9_vk_state.c';host=pc/'tests/vk_state_host.c';exe=out/'vk_state.exe'
    inputs=[Path(__file__).resolve(),source,host,pc/'native/d3d9_vk_state.h',
            *sorted((headers/'vulkan').glob('*.h')),*sorted((headers/'vk_video').glob('*.h'))]
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();frozen={str(p):sha(p) for p in inputs}
    subprocess.run([*args.cc,'-O2','-std=c11','-Wall','-Wextra','-Werror','-I'+str(pc/'native'),'-I'+str(headers),str(source),str(host),'-o',str(exe)],check=True)
    result=subprocess.run([str(exe)],check=True,capture_output=True,text=True,timeout=30);report=json.loads(result.stdout)
    assert report['status']=='PASS' and report['depth_checks']==48 and report['sampler_checks']==771
    assert all(sha(Path(p))==h for p,h in frozen.items());report.update(source_sha256=frozen,exe_sha256=sha(exe),game_executed=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(result.stdout.strip())

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('headers','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

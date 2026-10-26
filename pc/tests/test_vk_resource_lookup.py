"""Exercise actual Vulkan resource lookups without creating a GPU or playing audio."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from test_windows_heap import compiler_environment


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',nargs='+',required=True)
    p.add_argument('--headers',type=Path,required=True)
    p.add_argument('--backend-dir',type=Path)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    pc=Path(__file__).resolve().parents[1];backend=(a.backend_dir or pc/'native').resolve()
    dependencies=[pc/'native'/n for n in ('d3d9_geometry.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_vk_state.c','d3d9_constants.c')]
    files=[Path(__file__).resolve(),pc/'tests/vk_resource_lookup_host.c',backend/'d3d9_vk_backend.c',*dependencies,*[x for x in (pc/'native').iterdir() if x.suffix in ('.h','.inc')]]
    sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
    frozen={str(x):sha(x) for x in files};exe=out/'lookup-test.exe'
    command=[*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-fno-strict-aliasing','-ffunction-sections','-fdata-sections',
        '-I'+str(backend),'-I'+str(pc/'native'),'-I'+str(a.headers.resolve()),str(pc/'tests/vk_resource_lookup_host.c'),*[str(x) for x in dependencies],'-Wl,--gc-sections','-o',str(exe)]
    env=compiler_environment(a.cc);r=subprocess.run(command,capture_output=True,text=True,env=env,timeout=90)
    (out/'compiler.log').write_text(r.stdout+r.stderr);r.check_returncode()
    r=subprocess.run([str(exe)],capture_output=True,text=True,env=env,timeout=90)
    (out/'host.log').write_text(r.stdout+r.stderr);r.check_returncode();report=json.loads(r.stdout)
    assert all(sha(Path(x))==h for x,h in frozen.items())
    report.update(source_sha256=frozen,compiler_command=command,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('source_sha256','compiler_command')}))


if __name__=='__main__':main()

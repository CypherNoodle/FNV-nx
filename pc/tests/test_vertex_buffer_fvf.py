"""Windows oracle for VB FVF metadata and implicit state behavior."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def run(a):
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);pc=Path(__file__).resolve().parents[1];source=pc/'tests/vertex_buffer_fvf_host.c';compiler=Path(a.cc).resolve()
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();frozen={str(p):sha(p) for p in (Path(__file__).resolve(),source,compiler)}
    env=os.environ.copy();env['PATH']=str(compiler.parent)+os.pathsep+env['PATH']
    command=[str(compiler),'-std=c11','-O1','-Wall','-Wextra','-Werror',str(source),'-ld3d9','-luser32','-o',str(out/'oracle.exe')]
    b=subprocess.run(command,capture_output=True,text=True,env=env,timeout=120);(out/'compiler.log').write_text(b.stdout+b.stderr);assert not b.returncode,b.stderr
    p=subprocess.run([str(out/'oracle.exe')],capture_output=True,text=True,env=env,timeout=60);(out/'oracle.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stdout+p.stderr
    assert p.stdout.startswith('PASS6') and all(sha(Path(n))==h for n,h in frozen.items())
    result=dict(status='PASS',windows_hal=True,formats_checked=6,source_sha256=frozen,compiler_command=command,executable_sha256=sha(out/'oracle.exe'))
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',required=True);run(p.parse_args())

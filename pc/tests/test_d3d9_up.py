"""Check UP binding semantics with an independent Windows D3D9 HAL device."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def run(a):
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=Path(__file__).resolve().with_name('d3d9_up_host.c');exe=out/'up-reference.exe'
    inputs=[source,Path(__file__).resolve()]
    def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
    frozen={str(p):sha(p) for p in inputs}
    built=subprocess.run([*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror',str(source),'-ld3d9','-luser32','-o',str(exe)],capture_output=True,text=True)
    (out/'compiler.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise RuntimeError(built.stderr)
    tested=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60);(out/'run.log').write_text(tested.stdout+tested.stderr)
    if tested.returncode:raise RuntimeError(f'{tested.returncode}: '+tested.stdout+tested.stderr)
    result=json.loads(tested.stdout.strip().splitlines()[-1])
    assert result['status']=='PASS' and result['up_binding_checks']==4
    assert frozen=={str(p):sha(p) for p in inputs}
    result.update(source_sha256=frozen,exe_sha256=sha(exe),switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

"""Check SYSTEM_INFO32 against the real x86 Windows compiler/API ABI."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',nargs='+',required=True);a=p.parse_args()
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);source=Path(__file__).with_name('windows_system_layout.c').resolve()
    frozen={str(source):sha(source),str(Path(__file__).resolve()):sha(Path(__file__)),str(Path(a.cc[0]).resolve()):sha(Path(a.cc[0]))}
    exe=a.output/'layout-x86.exe';flags=['-target','x86-windows-gnu','-std=c11','-Wall','-Wextra','-Werror']
    run=subprocess.run([*a.cc,*flags,str(source),'-o',str(exe)],capture_output=True,text=True);(a.output/'compiler.log').write_text(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError(run.stderr)
    run=subprocess.run([str(exe.resolve())],capture_output=True,text=True,timeout=15);(a.output/'host.log').write_text(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError(run.stderr)
    observed=json.loads(run.stdout);assert observed==dict(bytes=36,architecture=0,page_bytes=4096,allocation_granule=65536)
    assert all(sha(Path(p))==h for p,h in frozen.items())
    result=dict(status='PASS',observed=observed,source_sha256=frozen,flags=flags,executable_sha256=sha(exe),switch_hardware_tested=False)
    (a.output/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(status='PASS',observed=observed)))

"""Native Windows oracle and real access-permission tests of the data VM."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def test(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve()
    if out.exists():raise ValueError('Use a fresh private directory')
    out.mkdir(parents=True)
    inputs=[Path(__file__).resolve(),pc/'tests/guest_memory_host.c',
        *[pc/'native'/name for name in ('guest_memory.c','guest_memory.h','host_mutex.h','memory_horizon.c','memory_horizon.h')]]
    frozen={p:sha(p) for p in inputs}
    command=[*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-I'+str(pc/'native'),
        str(pc/'native/guest_memory.c'),str(pc/'native/memory_horizon.c'),str(pc/'tests/guest_memory_host.c'),'-o',str(out/'vm-test.exe')]
    built=subprocess.run(command,capture_output=True,text=True)
    (out/'compiler.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise RuntimeError(built.stderr)
    executable=out/'vm-test.exe'
    run=subprocess.run([str(executable)],capture_output=True,text=True,timeout=60)
    (out/'host.log').write_text(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError(f'VM test exit {run.returncode}: '+run.stdout+run.stderr)
    result=json.loads(run.stdout.strip().splitlines()[-1]);faults=[]
    for mode in ('--noaccess','--readonly'):
        child=subprocess.run([str(executable),mode],capture_output=True,text=True,timeout=15)
        assert child.returncode==77,(mode,child.returncode,child.stderr)
        faults.append(mode)
    assert all(sha(p)==h for p,h in frozen.items())
    result.update(actual_access_violations=faults,source_sha256={str(p):h for p,h in frozen.items()},
        exe_sha256=sha(executable),native_engine_started=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',nargs='+',required=True)
    p.add_argument('--output',type=Path,required=True);test(p.parse_args())

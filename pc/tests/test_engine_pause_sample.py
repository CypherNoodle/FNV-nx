"""Verify bounded paused-target callback sequencing without pausing host threads."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def run(a):
    out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    if out.exists():raise ValueError('Use a fresh output directory')
    paths=[Path(__file__).resolve(),pc/'tests/engine_pause_sample_host.c',pc/'switch/engine_pause_sample.h',a.cc.resolve()]
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    hashes={str(p):sha(p) for p in paths};out.mkdir(parents=True)
    exe=out/'test.exe';command=[str(a.cc),'-std=c11','-O1','-Wall','-Wextra','-Werror','-I'+str(pc/'switch'),str(paths[1]),'-o',str(exe)]
    build=subprocess.run(command,capture_output=True,text=True);(out/'compiler.log').write_text(build.stdout+build.stderr)
    if build.returncode:raise RuntimeError(build.stderr)
    tested=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10);(out/'host.log').write_text(tested.stdout+tested.stderr)
    if tested.returncode:raise RuntimeError(tested.stdout+tested.stderr)
    result=json.loads(tested.stdout);assert result['status']=='PASS' and result['cases']==8
    assert all(sha(Path(p))==h for p,h in hashes.items())
    result.update(source_sha256=hashes,real_thread_pause_tested=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',type=Path,required=True);p.add_argument('--output',type=Path,required=True);run(p.parse_args())

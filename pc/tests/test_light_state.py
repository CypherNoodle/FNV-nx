"""Windows HAL light bank oracle; no game input or rendering."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def run(a):
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=Path(__file__).with_name('d3d9_light_host.c');compiler=Path(a.cc).resolve()
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    inputs={str(p):sha(p) for p in (Path(__file__).resolve(),source,compiler,source.with_name('light_bank_host.c'),source.parents[1]/'native/d3d9_light_state.h')}
    env=os.environ.copy();env['PATH']=str(compiler.parent)+os.pathsep+env['PATH']
    command=[str(compiler),'-std=c11','-O1','-Wall','-Wextra','-Werror','-fno-strict-aliasing','-I'+str(source.parents[1]/'native'),str(source),'-ld3d9','-luser32','-o',str(out/'oracle.exe')]
    build=subprocess.run(command,capture_output=True,text=True,env=env,timeout=120)
    (out/'compiler.log').write_text(build.stdout+build.stderr);assert not build.returncode,build.stderr
    result=subprocess.run([str(out/'oracle.exe')],capture_output=True,text=True,env=env,timeout=60)
    (out/'oracle.log').write_text(result.stdout+result.stderr);assert not result.returncode,result.stdout+result.stderr
    core_command=[str(compiler),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(source.parents[1]/'native'),str(source.with_name('light_bank_host.c')),'-o',str(out/'bank.exe')]
    core_build=subprocess.run(core_command,capture_output=True,text=True,env=env,timeout=120)
    (out/'bank-compiler.log').write_text(core_build.stdout+core_build.stderr);assert not core_build.returncode,core_build.stderr
    core=subprocess.run([str(out/'bank.exe')],capture_output=True,text=True,env=env,timeout=60)
    (out/'bank.log').write_text(core.stdout+core.stderr);assert not core.returncode,core.stdout+core.stderr
    report=json.loads(result.stdout);assert report['light_comparisons']==609
    assert all(sha(Path(n))==h for n,h in inputs.items())
    report.update(allocation_rollback_checked=True,sparse_budget_checked=True,field_masks_checked=True,source_sha256=inputs,compiler_command=command,executable_sha256=sha(out/'oracle.exe'))
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',required=True);run(p.parse_args())

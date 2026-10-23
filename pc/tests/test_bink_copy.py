"""Own bounded packed-pixel copy tests and optional read-only original DLL oracle.

The native oracle checks layout/rectangles, not FFmpeg pixel identity.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def run(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    sources=[pc/'native/bink_copy.c',pc/'tests/bink_copy_host.c']
    files=[Path(__file__),*sources,pc/'native/bink_copy.h',pc/'native/bink_decoder.h',a.cc.resolve()]
    if a.devkitpro:files.append(a.devkitpro.resolve()/'devkitA64/bin/aarch64-none-elf-gcc.exe')
    if bool(a.original_bink)!=bool(a.video):raise ValueError('Both DLL and video required')
    if a.original_bink:files.extend([a.original_bink.resolve(),a.video.resolve(),Path(__file__).with_name('bink_copy_x86_oracle.ps1')])
    frozen={str(p):sha(p) for p in files};env=os.environ.copy();env['PATH']=str(a.cc.resolve().parent)+os.pathsep+env['PATH']
    cmd=[str(a.cc.resolve()),'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-I'+str(pc/'native'),*map(str,sources),'-o',str(out/'copy-test.exe')]
    r=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=60);(out/'build.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
    r=subprocess.run([str(out/'copy-test.exe')],capture_output=True,text=True,env=env,timeout=30);(out/'test.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
    report=json.loads(r.stdout.strip().splitlines()[-1]);assert report['status']=='PASS'
    if a.devkitpro:
        cc=a.devkitpro.resolve()/'devkitA64/bin/aarch64-none-elf-gcc.exe'
        cmd=[str(cc),'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-D__SWITCH__','-mtp=soft','-I'+str(pc/'native'),'-c',str(sources[0]),'-o',str(out/'copy-arm64.o')]
        r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/'arm64.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
        report['arm64_object_sha256']=sha(out/'copy-arm64.o')
    if a.original_bink:
        cmd=['C:/Windows/SysWOW64/WindowsPowerShell/v1.0/powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(files[-1]),'-DllPath',str(a.original_bink.resolve()),'-VideoPath',str(a.video.resolve())]
        r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(out/'native-oracle.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
        native=json.loads(r.stdout.strip().splitlines()[-1]);assert native['status']=='PASS' and native['non_gray_pixels']>0
        report['native_oracle']=native
    assert all(sha(Path(n))==h for n,h in frozen.items()),'Inputs changed'
    report.update(source_sha256=frozen,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('cc','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--original-bink',type=Path);p.add_argument('--video',type=Path);p.add_argument('--devkitpro',type=Path)
    run(p.parse_args())

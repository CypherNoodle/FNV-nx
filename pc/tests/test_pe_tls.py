"""Bounded TLS metadata/template model tests; optional read-only installed EXE audit."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('fixture','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--game-exe',type=Path);p.add_argument('--cc',nargs='+',required=True);a=p.parse_args()
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve();assert not out.exists();out.mkdir(parents=True)
    sources=[pc/'native/pe_image.c',pc/'native/pe_tls.c',pc/'tests/pe_tls_host.c']
    files=[*sources,pc/'native/pe_image.h',pc/'native/pe_tls.h',Path(__file__).resolve(),a.fixture.resolve()]
    if a.game_exe:files.append(a.game_exe.resolve())
    frozen={str(f):sha(f) for f in files};exe=out/'tls-test.exe'
    built=subprocess.run([*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-I'+str(pc/'native'),*map(str,sources),'-o',str(exe)],capture_output=True,text=True)
    (out/'compiler.log').write_text(built.stdout+built.stderr);assert built.returncode==0,built.stderr
    run=subprocess.run([str(exe),str(a.fixture.resolve())],capture_output=True,text=True,timeout=30)
    (out/'host.log').write_text(run.stdout+run.stderr);assert run.returncode==0,run.stdout+run.stderr
    result=json.loads(run.stdout.strip());result.update(source_sha256=frozen,exe_sha256=sha(exe),switch_hardware_tested=False)
    if a.game_exe:
        audit=subprocess.run([str(exe),str(a.game_exe.resolve()),'--audit'],capture_output=True,text=True,timeout=30)
        (out/'game-audit.log').write_text(audit.stdout+audit.stderr);assert audit.returncode==0,audit.stdout+audit.stderr
        result['read_only_game_tls']=json.loads(audit.stdout)
    assert all(sha(Path(f))==digest for f,digest in frozen.items())
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))

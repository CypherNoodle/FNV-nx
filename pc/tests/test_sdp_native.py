"""Native SDP structural reader versus independent Python parser; no rendering."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from shader_packages import parse_package

def run(a):
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    pc=Path(__file__).resolve().parents[1];cc=Path(a.cc).resolve()
    sources=[pc/'native/d3d9_sdp.c',pc/'native/d3d9_sdp.h',pc/'tests/sdp_host.c',Path(__file__).resolve(),pc/'tools/shader_packages.py']
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    frozen={str(p):sha(p) for p in sources}
    env=os.environ.copy();env['PATH']=str(cc.parent)+os.pathsep+env['PATH']
    exe=out/'sdp.exe';cmd=[str(cc),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(pc/'native'),str(sources[0]),str(sources[2]),'-o',str(exe)]
    b=subprocess.run(cmd,capture_output=True,text=True,env=env);(out/'compile.log').write_text(b.stdout+b.stderr);assert not b.returncode,b.stderr
    def check(path,budget=67108864,valid=True):
        data=path.read_bytes()
        p=subprocess.run([str(exe),str(path),str(budget)],capture_output=True,text=True,env=env,timeout=30)
        assert not p.returncode,p.stderr
        rows=p.stdout.splitlines()
        if not valid:assert rows[0]!='0',p.stdout;return
        records=parse_package(data);assert rows[:2]==['0',str(len(records))]
        expected=[]
        for r in records:
            h=2166136261
            for byte in r['code']:h=((h^byte)*16777619)&0xffffffff
            expected.append(f"{r['bytes']} {h:08x}")
        assert rows[2:]==expected
    payload=b''
    for i,v in enumerate((0xfffe0101,0xffff0103,0xfffe0200,0xffff0201,0xfffe0300,0xffff0300)):
        code=struct.pack('<III',v,0,0xffff)
        payload+=(f'synthetic{i}'.encode()+b'\0').ljust(256,b'\0')+struct.pack('<I',len(code))+code
    good=struct.pack('<III',100,6,len(payload))+payload
    fixture=out/'synthetic.sdp';fixture.write_bytes(good);check(fixture)
    empty=out/'empty.sdp';empty.write_bytes(struct.pack('<III',100,0,0));check(empty)
    mutations=[good[:11],good+b'X',good[:-1]]
    for offset,value in ((0,99),(4,9999),(8,1),(268,7),(272,0),(280,0)):
        b=bytearray(good);struct.pack_into('<I',b,offset,value);mutations.append(bytes(b))
    for name in (b'\0'*256,b'X'*256,b'\xff\0'+b'\0'*254):
        b=bytearray(good);b[12:268]=name;mutations.append(bytes(b))
    for i,b in enumerate(mutations):
        path=out/f'bad{i}.sdp';path.write_bytes(b);check(path,valid=False)
    check(fixture,len(good),False);check(fixture,0,False)
    real=[]
    if a.shaders:
        for path in sorted(a.shaders.glob('*.sdp')):
            before=sha(path);check(path);assert sha(path)==before
            real.append(dict(file=path.name,sha256=before,records=len(parse_package(path.read_bytes()))))
        assert real
    assert all(sha(Path(p))==h for p,h in frozen.items())
    report=dict(status='PASS',synthetic_packages=2,negative_cases=len(mutations)+2,real_packages=real,source_sha256=frozen,translated=False,engine_started=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',required=True);p.add_argument('--shaders',type=Path);run(p.parse_args())

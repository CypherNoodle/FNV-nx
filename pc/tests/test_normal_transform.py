"""Independent rational inverse/transpose model for bounded normal matrices."""
import argparse
import ctypes as C
from fractions import Fraction as F
import hashlib
import json
from pathlib import Path
import random
import subprocess


def inverse(a):
    m=[list(map(F,a[r]))+[F(int(r==c)) for c in range(3)] for r in range(3)]
    for c in range(3):
        p=next((r for r in range(c,3) if m[r][c]),None)
        if p is None:return None
        m[c],m[p]=m[p],m[c];s=m[c][c];m[c]=[v/s for v in m[c]]
        for r in range(3):
            if r!=c:
                k=m[r][c];m[r]=[v-k*x for v,x in zip(m[r],m[c])]
    return [r[3:] for r in m]


def run(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    source=pc/'tests/normal_transform_host.c';binary=out/'normal.dll'
    inputs=[Path(__file__).resolve(),source,*[pc/'native'/n for n in ('d3d9_normal_transform.h','d3d9_fixed_vertex.h','d3d9_transform_state.h')]]
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();frozen={str(p):sha(p) for p in inputs}
    cmd=[a.cc,'-shared','-std=c11','-O2','-Wall','-Wextra','-Werror','-ffp-contract=off','-Wl,--export-all-symbols','-I'+str(pc/'native'),str(source),'-o',str(binary)]
    p=subprocess.run(cmd,capture_output=True,text=True);(out/'compiler.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stderr
    lib=C.CDLL(str(binary));fn=lib.fnv_test_normal;fn.argtypes=[C.POINTER(C.c_float)]*3;fn.restype=C.c_uint32
    rng=random.Random(319);checks=0;negative=0
    for _ in range(2400):
        matrices=[]
        for stage in range(2):
            m=[0.0]*16;m[15]=1
            for r in range(3):
                for c in range(3):m[r*4+c]=rng.randrange(-4,5)*.5
                m[12+r]=rng.randrange(-5,6)
            matrices.append(m)
        w,v=matrices
        # Half-integer inputs make world*view exact in binary32 here.
        product=[[sum(F(w[r*4+k])*F(v[k*4+c]) for k in range(3)) for c in range(3)] for r in range(3)]
        inv=inverse(product);output=(C.c_float*12)(*[123.0]*12)
        hr=fn((C.c_float*16)(*w),(C.c_float*16)(*v),output)
        if inv is None:
            assert hr==0x8876086c and list(output)==[123.0]*12;negative+=1;continue
        assert not hr
        expected=[float(inv[col][r]) if r<3 else 0.0 for col in range(3) for r in range(4)]
        for got,want in zip(output,expected):assert abs(got-want)<=max(1e-7,abs(want)*1e-6),(got,want)
        checks+=1
    identity=[float(r==c) for r in range(4) for c in range(4)]
    for index,value in ((0,float('nan')),(5,float('inf')),(3,.5),(15,2.0)):
        bad=identity.copy();bad[index]=value;output=(C.c_float*12)(*[123.0]*12)
        assert fn((C.c_float*16)(*bad),(C.c_float*16)(*identity),output)==0x8876086c and list(output)==[123.0]*12;negative+=1
    output=(C.c_float*12)(*[123.0]*12);assert fn(None,(C.c_float*16)(*identity),output)==0x8876086c
    assert all(sha(Path(n))==h for n,h in frozen.items())
    report=dict(status='PASS',rational_comparisons=checks,rejections=negative,source_sha256=frozen,compiler_command=cmd,binary_sha256=sha(binary),gpu_lighting_tested=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--cc',required=True);run(p.parse_args())

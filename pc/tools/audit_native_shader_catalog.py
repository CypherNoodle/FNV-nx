"""Validate private hashed SM1/2/3 shaders by actual Windows D3D9 NULLREF creation.

No shader drawing, SPIR-V translation, game execution or capability advertisement.
Private output stores identities/results only; bytecode remains in its input root.
"""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
from translate_d3d9_pair import normalize


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def audit(a):
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    manifest=a.manifest.resolve();raw=a.raw.resolve();keys=manifest.read_text(encoding='ascii').splitlines()
    if not 0<len(keys)<=8192 or len(keys)!=len(set(keys)) or any(not re.fullmatch('[0-9a-f]{64}',h) for h in keys):raise ValueError('Invalid manifest')
    own=Path(__file__).with_name('native_shader_validator.c');compiler=Path(a.cc).resolve()
    frozen={str(p):sha(p) for p in (manifest,own,Path(__file__),Path(normalize.__code__.co_filename),compiler,Path(os.environ['SystemRoot'])/'System32/d3d9.dll',Path(os.environ['SystemRoot'])/'System32/d3dcompiler_47.dll')}
    blobs=[]
    for h in keys:
        p=raw/(h+'.bin')
        if not 8<=p.stat().st_size<=1048576 or sha(p)!=h:raise ValueError('Shader identity/size mismatch')
        data=p.read_bytes()
        if (int.from_bytes(data[:4],'little')>>8)&255 not in (1,2,3):raise ValueError('Unsupported shader model')
        normalize(data)  # Preserve original; native bounded disassembly also checks SM1.
        frozen[str(p)]=h;blobs.append(data)
    dllpath=out/'validator.dll';env=os.environ.copy();env['PATH']=str(compiler.parent)+os.pathsep+env['PATH']
    command=[str(compiler),'-std=c11','-O2','-Wall','-Wextra','-Werror','-shared',str(own),'-ld3d9','-luser32','-o',str(dllpath)]
    run=subprocess.run(command,capture_output=True,text=True,env=env,timeout=90);(out/'build.log').write_text(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError(run.stderr)
    dll=C.CDLL(str(dllpath));dll.shader_validator_open.restype=C.c_uint32;dll.shader_validator_check.argtypes=[C.c_void_p,C.c_uint32];dll.shader_validator_check.restype=C.c_uint32
    hr=dll.shader_validator_open()
    if hr:raise RuntimeError('D3D9 NULLREF init: %08x'%hr)
    rows=[]
    try:
        for h,data in zip(keys,blobs):
            code=C.create_string_buffer(data);hr=dll.shader_validator_check(code,len(data));rows.append(dict(sha256=h,hresult=hr,accepted=not hr))
    finally:dll.shader_validator_close()
    if any(sha(Path(p))!=h for p,h in frozen.items()):raise RuntimeError('Input changed')
    report=dict(status='COMPLETE',total=len(rows),accepted=sum(r['accepted'] for r in rows),results=rows,source_sha256=frozen,validator_sha256=sha(dllpath),gpu_drawn=False,translated=False,game_started=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('status','total','accepted','gpu_drawn','translated')}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('manifest','raw','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',required=True);audit(p.parse_args())

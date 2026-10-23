"""Own-input prepared bundle emitter plus native Vulkan initialization checks."""
import argparse
import copy
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
from test_d3d9_spirv import VS,PS,compile_hlsl
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from translate_d3d9_pair import translate
from emit_prepared_bundle import emit_bundle

def run(a):
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);pc=Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    dll=C.WinDLL(str(Path(os.environ['SystemRoot'])/'System32/d3dcompiler_47.dll'))
    for i in range(2):
        f=out/('fixture%d'%i);f.mkdir();(f/'vs.bin').write_bytes(compile_hlsl(dll,VS,b'vs_3_0'))
        ps=PS if not i else PS.replace(b'return tex2D(image,uv)*tint;',b'return 1-tex2D(image,uv)*tint;')
        (f/'ps.bin').write_bytes(compile_hlsl(dll,ps,b'ps_3_0'));(f/'attributes.txt').write_text('0 0 1\n5 0 1\n')
        translate(SimpleNamespace(vs=f/'vs.bin',ps=f/'ps.bin',attributes=f/'attributes.txt',linker=a.linker.resolve(),output=f/'pair'))
    assert (out/'fixture0/pair/vs.spv').read_bytes()==(out/'fixture1/pair/vs.spv').read_bytes()
    row=lambda i:dict(fixture='fixture%d'%i,inputs=[[0,0,0],[5,0,1]],sampler_mask=1)
    request=dict(schema=1,programs=[row(0),row(1),row(0)]);manifest=out/'manifest.json';manifest.write_text(json.dumps(request))
    proof=emit_bundle(manifest,out/'bundle.h');assert proof['programs']==2 and proof['duplicate_programs']==1
    assert (out/'bundle.h').read_text().count('static const uint32_t ')==6 # three bytecodes, three linked modules
    try:emit_bundle(manifest,out/'bundle.h')
    except FileExistsError:pass
    else:raise AssertionError('bundle overwritten')
    negative=0
    def reject(name,data):
        nonlocal negative
        m=out/(name+'.json');m.write_text(json.dumps(data));target=out/(name+'.h')
        try:emit_bundle(m,target)
        except (ValueError,FileNotFoundError):negative+=1
        else:raise AssertionError('invalid bundle accepted: '+name)
        assert not target.exists()
    reject('empty',dict(schema=1,programs=[]))
    reject('root-list',[])
    reject('schema-bool',dict(schema=True,programs=[row(0)]))
    reject('root-extra',dict(schema=1,programs=[row(0)],extra=0))
    conflict=copy.deepcopy(request);conflict['programs'][2]['inputs']=[[0,0,1],[5,0,0]];reject('conflict',conflict)
    bad=copy.deepcopy(request);bad['programs'][1]['sampler_mask']=2;reject('mask',bad)
    bad=copy.deepcopy(request);bad['programs'][1]['inputs']=[[0,0,0],[5,0,0]];reject('map',bad)
    bad=copy.deepcopy(request);bad['programs'][1]['fixture']='missing';reject('missing',bad)
    shader=out/'fixture1/ps.bin';saved=shader.read_bytes();shader.write_bytes(saved[:-4]+bytes(4));reject('identity',request);shader.write_bytes(saved)
    native=[pc/'native'/n for n in ('d3d9_vk_backend.c','d3d9_vk_prepared.c','d3d9_geometry.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_vk_state.c','d3d9_constants.c')]
    files=[Path(__file__).resolve(),Path(emit_bundle.__code__.co_filename),pc/'tools/emit_prepared_program.py',pc/'tests/prepared_bundle_host.c',out/'bundle.h',a.cc.resolve(),a.linker.resolve(),*native,*[p for p in (pc/'native').iterdir() if p.suffix in ('.h','.inc')]]
    frozen={str(p):sha(p) for p in files};frozen.update(proof['input_sha256'])
    env=os.environ.copy();env['PATH']=str(a.cc.resolve().parent)+os.pathsep+env['PATH']
    cmd=[str(a.cc.resolve()),'-std=c11','-O1','-Wall','-Wextra','-Werror','-fno-strict-aliasing','-ffp-contract=off','-I'+str(pc/'native'),'-I'+str(out),'-I'+str(a.headers.resolve()),str(pc/'tests/prepared_bundle_host.c'),*map(str,native),'-o',str(out/'bundle-test.exe')]
    r=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=120);(out/'compiler.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
    r=subprocess.run([str(out/'bundle-test.exe')],capture_output=True,text=True,env=env,timeout=60);(out/'host.log').write_text(r.stdout+r.stderr);assert not r.returncode and 'PASS bundle preflight' in r.stdout,r.stdout+r.stderr
    assert all(sha(Path(p))==h for p,h in frozen.items())
    result=dict(status='PASS',emitter=proof,negative_cases=negative,overwrite_rejected=True,repeated_batches=64,missing_membership_no_mutation=True,partial_failure_teardown=True,game_data_used=False,engine_started=False,switch_hardware_tested=False,source_sha256=frozen)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k not in ('emitter','source_sha256')}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('output','linker','headers','cc'):p.add_argument('--'+n,type=Path,required=True)
    run(p.parse_args())

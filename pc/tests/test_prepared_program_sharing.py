"""Two validated private pairs share one exact VS on the same Vulkan device.

Synthetic resources and Windows D3D9 pixel oracle; not an engine draw.
The caller supplies both fixtures. Original data and generated headers stay private.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
from types import SimpleNamespace
from test_d3d9_com import test
from test_catalog_pair_com import CATALOG_SLOTS
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from emit_prepared_program import emit

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def run(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    fixtures=[a.first.resolve(),a.second.resolve()];proofs=[];inputs=[[0,0,0],[5,0,1]]
    frozen={}
    for i,f in enumerate(fixtures):
        proof=json.loads((f/'validation.json').read_text())
        if proof['status']!='PASS' or proof['pixel_checks']!=6144:raise ValueError('Require two accepted pixel fixtures')
        accepted=emit(f,inputs,1,out/('verified%d.h'%i));frozen.update(accepted['input_sha256'])
        for s in ('vs','ps'):
            if sha(f/(s+'.bin'))!=proof['shader_sha256'][s] or sha(f/'pair'/(s+'.spv'))!=proof['spirv_sha256'][s]:raise ValueError('Fixture shader identity changed')
        proofs.append(json.loads((f/'pair/translation.json').read_text()))
        frozen[str(f/'validation.json')]=sha(f/'validation.json')
    for name in ('vs.bin','pair/vs.spv'):
        if (fixtures[0]/name).read_bytes()!=(fixtures[1]/name).read_bytes():raise ValueError('Need identical VS code and linked module')
    if proofs[0]['stages']['vs']['uniform_plan']!=proofs[1]['stages']['vs']['uniform_plan']:raise ValueError('VS plans differ')
    if (fixtures[0]/'ps.bin').read_bytes()==(fixtures[1]/'ps.bin').read_bytes():raise ValueError('Need distinct pixel programs')
    def plan(name,p):
        entries=p['entries'];pointer=name+'_entries' if entries else 'NULL'
        body='static const struct fnv_uniform_entry '+pointer+'[]={'+','.join('{'+','.join(str(e[k]) for k in ('kind','first','count','offset','stride'))+'}' for e in entries)+'};\n' if entries else ''
        return body+'static const struct fnv_uniform_plan '+name+'={'+str(p['bytes'])+','+str(len(entries))+','+pointer+'};\n'
    header='#define FNV_CATALOG_CONSTANTS 1\n#define FNV_CATALOG_SHARED_PROGRAM 1\n#define FNV_CATALOG_SAMPLER_MASK 1\n'
    header+=plan('vertex_plan',proofs[0]['stages']['vs']['uniform_plan'])+plan('pixel_plan',proofs[0]['stages']['ps']['uniform_plan'])+plan('shared_pixel_plan',proofs[1]['stages']['ps']['uniform_plan'])
    header+='static const struct fnv_vk_vertex_input vertex_inputs[]={{0,0,0},{5,0,1}};\n'
    for name,file in (('shared_ps','ps.bin'),('shared_ps_spv','pair/ps.spv')):
        data=(fixtures[1]/file).read_bytes();header+='static const uint32_t '+name+'[]={'+','.join(hex(w) for w in struct.unpack('<%dI'%(len(data)//4),data))+'};\n'
    (out/'catalog_pair_fixture.h').write_text(header,newline='\n')
    native=[pc/'native'/n for n in ('d3d9_vk_backend.c','d3d9_vk_prepared.c','d3d9_geometry.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_vk_state.c','d3d9_constants.c')]
    files=[Path(__file__).resolve(),Path(emit.__code__.co_filename),pc/'tests/catalog_pair_gpu_host.c',pc/'tests/catalog_pair_oracle.h',pc/'tests/catalog_mip_fixture.h',out/'catalog_pair_fixture.h',a.cc.resolve(),*native,*[p for p in (pc/'native').iterdir() if p.suffix in ('.h','.inc')],*a.headers.resolve().rglob('*.h')]
    frozen.update({str(p):sha(p) for p in files})
    env=os.environ.copy();env['PATH']=str(a.cc.resolve().parent)+os.pathsep+env['PATH']
    cmd=[str(a.cc.resolve()),'-std=c11','-O1','-Wall','-Wextra','-Werror','-fno-strict-aliasing','-ffp-contract=off','-I'+str(pc/'native'),'-I'+str(out),'-I'+str(a.headers.resolve()),str(pc/'tests/catalog_pair_gpu_host.c'),*map(str,native),'-ld3d9','-luser32','-o',str(out/'gpu-test.exe')]
    result=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=120);(out/'compiler.log').write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(result.stderr)
    args=[str(out/'gpu-test.exe'),*[str(fixtures[0]/f) for f in ('vs.bin','ps.bin','pair/vs.spv','pair/ps.spv')]]
    controls=('wrong-shared-program','wrong-uv','wrong-texture','wrong-vs-constants','wrong-ps-constants','missing-pixel-center')
    for mode in ('normal',*controls):
        result=subprocess.run(args+([] if mode=='normal' else [mode]),capture_output=True,text=True,env=env,timeout=60)
        (out/(mode+'.log')).write_text(result.stdout+result.stderr)
        if mode=='normal':assert not result.returncode and 'PASS catalog pair 6144 pixels' in result.stdout,result.stdout+result.stderr
        else:assert result.returncode and 'catalog pixel mismatch' in result.stderr,(mode,result.stdout,result.stderr)
    com_report=None
    if bool(a.sdk)!=bool(a.python_deps):raise ValueError('Supply both SDK and Python dependencies')
    if a.sdk:
        shaders=out/'shaders';shaders.mkdir();records=[]
        for label,file in (('vs',fixtures[0]/'vs.bin'),('ps0',fixtures[0]/'ps.bin'),('ps1',fixtures[1]/'ps.bin')):
            data=file.read_bytes();records.append(label.encode().ljust(256,b'\0')+struct.pack('<I',len(data))+data)
        data=b''.join(records);(shaders/'private.sdp').write_bytes(struct.pack('<III',100,3,len(data))+data)
        opts=SimpleNamespace(sdk=a.sdk,python_deps=a.python_deps,output=out/'com',shaders=shaders,cc=[str(a.cc.resolve())])
        test(opts,host_source='catalog_pair_com_host.c',slots=CATALOG_SLOTS,modes=('normal',),strict_fault_cases=0,extra_args=tuple(fixtures[0]/f for f in ('vs.bin','ps.bin','pair/vs.spv','pair/ps.spv')),
             extra_inputs=files,extra_sources=[p for p in native if p.name!='d3d9_constants.c'],extra_includes=(a.headers.resolve(),out),baseline_lifetime_checks=False)
        com_report=json.loads((out/'com/validation.json').read_text());assert com_report['pixel_checks']==6144 and com_report['synthetic_functions']==40
        for mode in controls[:-1]:
            result=subprocess.run([str(out/'com/com-test.exe'),str(out/'com/fixture.exe'),str(out/'com/catalog.bin'),mode,*args[1:]],capture_output=True,text=True,env=env,timeout=60)
            (out/('com-'+mode+'.log')).write_text(result.stdout+result.stderr)
            assert result.returncode and 'translated catalog pixel mismatch' in result.stderr,(mode,result.stdout,result.stderr)
    assert all(sha(Path(p))==h for p,h in frozen.items())
    report=dict(status='PASS',pixel_checks=6144,negative_controls=list(controls),repeated_preparations=64,unique_stages=3,distinct_programs=2,source_sha256=frozen,original_data_used=all(json.loads((f/'validation.json').read_text())['original_package_used'] for f in fixtures),runtime_pair_observed=False,engine_started=False,switch_hardware_tested=False)
    if com_report:report['translated_com']={k:com_report[k] for k in ('synthetic_functions','pixel_checks','exe_sha256','generated_source_sha256')}
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('first','second','headers','output','cc'):p.add_argument('--'+n,type=Path,required=True)
    for n in ('sdk','python-deps'):p.add_argument('--'+n,type=Path)
    run(p.parse_args())

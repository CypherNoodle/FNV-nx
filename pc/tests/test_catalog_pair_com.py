"""Explicit prepared pair -> SDK-translated x86 COM calls -> Vulkan pixels.

Bytecode/SPIR-V from the caller's private fixture remain in private outputs.
This does not execute the game or observe an actual engine shader pairing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from types import SimpleNamespace
from test_d3d9_com import test,SLOTS

def run(a):
    fixture=a.fixture.resolve();out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    proof=json.loads((fixture/'validation.json').read_text());sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    assert proof['status']=='PASS' and proof['float_constants'] and proof['pixel_checks']==6144 and proof['negative_controls']==5
    for stage in ('vs','ps'):
        assert sha(fixture/(stage+'.bin'))==proof['shader_sha256'][stage]
        assert sha(fixture/'pair'/(stage+'.spv'))==proof['spirv_sha256'][stage]
    out.mkdir(parents=True,exist_ok=False);shaders=out/'shaders';shaders.mkdir();records=[]
    for stage in ('vs','ps'):
        code=(fixture/(stage+'.bin')).read_bytes();records.append(stage.encode().ljust(256,b'\0')+struct.pack('<I',len(code))+code)
    payload=b''.join(records);(shaders/'private.sdp').write_bytes(struct.pack('<III',100,2,len(payload))+payload)
    sources=[pc/'native'/n for n in ('d3d9_vk_backend.c','d3d9_vk_prepared.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_geometry.c','d3d9_vk_state.c')]
    extra=[Path(__file__).resolve(),pc/'tests/catalog_pair_oracle.h',fixture/'catalog_pair_fixture.h',fixture/'validation.json',
           *[p for p in (pc/'native').iterdir() if p.suffix in ('.h','.inc')],*a.headers.rglob('*.h')]
    slots=SLOTS+[(92,2),(107,2),(86,3),(87,2),(26,7),(11,5),(12,1),(100,5),(23,9),(19,5),(20,2),(65,3),(57,3),(43,7),(47,2),(41,1),(42,1),(81,4),(93,2),(108,2)]
    shader_args=tuple(fixture/n for n in ('vs.bin','ps.bin','pair/vs.spv','pair/ps.spv'))
    opts=SimpleNamespace(sdk=a.sdk,python_deps=a.python_deps,output=out/'com',shaders=shaders,cc=a.cc)
    test(opts,host_source='catalog_pair_com_host.c',slots=slots,modes=('normal',),strict_fault_cases=0,extra_args=shader_args,
         extra_inputs=extra,extra_sources=sources,extra_includes=(a.headers,fixture),baseline_lifetime_checks=False)
    report=json.loads((out/'com/validation.json').read_text())
    assert report['translated_catalog_draw'] and report['pixel_checks']==6144 and report['synthetic_functions']==40
    controls={}
    for mode in ('wrong-uv','wrong-texture','wrong-vs-constants','wrong-ps-constants'):
        p=subprocess.run([str(out/'com/com-test.exe'),str(out/'com/fixture.exe'),str(out/'com/catalog.bin'),mode,*map(str,shader_args)],capture_output=True,text=True,timeout=60)
        (out/(mode+'.log')).write_text(p.stdout+p.stderr);assert p.returncode and 'translated catalog pixel mismatch' in p.stderr
        controls[mode]=True
    report.update(negative_controls=controls,original_package_used=proof['original_package_used'],runtime_pair_observed=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ('status','pixel_checks','synthetic_functions','original_package_used')}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','headers','fixture','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

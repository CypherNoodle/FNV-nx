"""Prepared own shader fixture -> synthetic x86 COM callers -> Vulkan pixels."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
from types import SimpleNamespace
from test_d3d9_com import test, SLOTS


def run(a):
    pc=Path(__file__).resolve().parents[1];fixture=a.fixture.resolve();out=a.output.resolve()
    proof=json.loads((fixture/'validation.json').read_text())
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    assert proof['status']=='PASS' and proof['negative_controls']=={'wrong-transpose':True,'wrong-order':True}
    output_hashes={k.replace('\\','/'):v for k,v in proof['output_sha256'].items()}
    for name in ('vs.bin','ps.bin','pair/vs.spv','pair/ps.spv','fixed_vertex_fixture.h'):
        assert sha(fixture/name)==output_hashes[name]
    for name in ('shaders/fixed_unlit.vs.hlsl','tests/fixed_vertex_uv.ps.hlsl'):
        assert sha(pc/name)==proof['source_sha256'][str(pc/name)]
    out.mkdir(parents=True,exist_ok=False);shaders=out/'shaders';shaders.mkdir()
    records=[]
    for stage in ('vs','ps'):
        code=(fixture/(stage+'.bin')).read_bytes();records.append((stage.encode()+bytes(256-len(stage)))+struct.pack('<I',len(code))+code)
    payload=b''.join(records);(shaders/'own.sdp').write_bytes(struct.pack('<III',100,2,len(payload))+payload)
    headers=a.headers.resolve()
    sources=[pc/'native'/n for n in ('d3d9_vk_backend.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_geometry.c','d3d9_vk_state.c')]
    extra=[Path(__file__).resolve(),fixture/'fixed_vertex_fixture.h',fixture/'validation.json',
           *[p for p in (pc/'native').iterdir() if p.suffix in ('.h','.inc')],*headers.rglob('*.h')]
    args=SimpleNamespace(sdk=a.sdk,python_deps=a.python_deps,output=out/'com',shaders=shaders,cc=a.cc)
    slots=SLOTS+[(107,2),(89,2),(26,7),(11,5),(12,1),(100,5),(57,3),(41,1),(42,1),(81,4),(44,3)]
    shader_args=tuple(fixture/n for n in ('vs.bin','ps.bin','pair/vs.spv','pair/ps.spv'))
    test(args,host_source='fixed_vertex_com_host.c',slots=slots,modes=('normal',),strict_fault_cases=0,
         extra_args=shader_args,extra_inputs=extra,extra_sources=sources,extra_includes=(headers,fixture),baseline_lifetime_checks=False)
    report=json.loads((out/'com/validation.json').read_text())
    assert report['translated_fixed_draw'] and report['pixel_checks']==3072 and report['synthetic_functions']==31
    controls={}
    for mode,frame in (('ignore-matrices',1),('clobber-constants',2)):
        command=[str(out/'com/com-test.exe'),str(out/'com/fixture.exe'),str(out/'com/catalog.bin'),mode,*map(str,shader_args)]
        bad=subprocess.run(command,capture_output=True,text=True,timeout=60)
        (out/(mode+'.log')).write_text(bad.stdout+bad.stderr)
        assert bad.returncode and ('translated fixed pixel mismatch frame='+str(frame)) in bad.stderr
        controls[mode]=True
    report['negative_controls']=controls
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS translated COM fixed draw:3072 pixels; matrix and constant-isolation controls detected')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','headers','fixture','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

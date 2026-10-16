"""SDK x86 COM callers -> bank/version manager -> Vulkan pixels and real fence.

Constants, VB/IB and ARGB8 texture uploads/bindings are connected through COM.
Depth and sampler parameters also come from COM; variant selection, other
states and targets remain explicit. This is not a full provider.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
from types import SimpleNamespace
from test_d3d9_com import test,SLOTS,sha


def run(args):
    pc=Path(__file__).resolve().parents[1];fixture=args.uniform_fixture.resolve()
    translation=json.loads((fixture/'pair/translation.json').read_text(encoding='utf-8'))
    assert translation['status']=='PASS' and translation['game_executed'] is False
    inputs=Path(str(args.output.resolve())+'-inputs');inputs.mkdir(parents=True,exist_ok=False)
    shaders=inputs/'shaders';shaders.mkdir()
    body=b'';text=['#include "d3d9_uniform_pack.h"\n']
    for index,stage in enumerate(('vs','ps')):
        path=fixture/(stage+'.bin');data=path.read_bytes()
        assert sha(path)==translation['stages'][stage]['original_sha256']
        name=(stage+'.'+('vso' if index==0 else 'pso')).encode();name=name+b'\0'*(256-len(name))
        body+=name+struct.pack('<I',len(data))+data
        text.append('#define FIXTURE_%s %s\n'%(stage.upper(),json.dumps(path.as_posix())))
        plan=translation['stages'][stage]['uniform_plan']
        text.append('static const struct fnv_uniform_entry entries_%s[]={%s};\n'%(stage,','.join(
            '{%du,%du,%du,%du,%du}'%tuple(e[n] for n in ('kind','first','count','offset','stride')) for e in plan['entries'])))
    (shaders/'own.sdp').write_bytes(struct.pack('<3I',100,2,len(body))+body)
    text.append('static const struct fnv_uniform_plan fixture_plans[2]={'+','.join(
        '{%du,%du,entries_%s}'%(translation['stages'][s]['uniform_plan']['bytes'],len(translation['stages'][s]['uniform_plan']['entries']),s)
        for s in ('vs','ps'))+'};\n')
    header=inputs/'uniform_fixture.h';header.write_text(''.join(text),encoding='ascii')
    gpu=fixture/'gpu-fixture';modules=(gpu/'normal.vs.spv',gpu/'normal.ps.spv')
    for path,stage in zip(modules,('vs','ps')):assert sha(path)==translation['stages'][stage]['spirv']['sha256']
    native=pc/'native';headers=args.headers.resolve()
    sources=[native/'d3d9_uniform_state.c',native/'d3d9_uniform_pack.c',native/'d3d9_geometry.c',native/'d3d9_texture_upload.c',native/'d3d9_vk_state.c']
    extra=[Path(__file__),header,fixture/'vs.bin',fixture/'ps.bin',fixture/'pair/translation.json',
           native/'d3d9_uniform_state.h',native/'d3d9_uniform_pack.h',native/'d3d9_geometry.h',
           pc/'tests/d3d9_geometry_gpu_fixture.h',pc/'tests/d3d9_texture_gpu_fixture.h',
           native/'d3d9_texture_upload.h',native/'d3d9_vk_state.h',pc/'tests/d3d9_state_gpu_fixture.h',pc/'tests/vulkan_pipeline_host.c',
           Path(os.environ['SystemRoot'])/'System32/d3d9.dll',Path(os.environ['SystemRoot'])/'System32/vulkan-1.dll',
           *sorted((headers/'vulkan').glob('*.h')),*sorted((headers/'vk_video').glob('*.h'))]
    opts=SimpleNamespace(sdk=args.sdk,python_deps=args.python_deps,output=args.output,shaders=shaders,cc=args.cc)
    test(opts,host_source='d3d9_uniform_state_host.c',slots=SLOTS+[(41,1),(42,1),(82,7),(107,2),
         (26,7),(27,7),(11,5),(12,1),(100,5),(104,2),(86,3),(87,2),
         (23,9),(19,5),(20,2),(65,3),(64,3),(57,3),(58,3),(69,4),(68,4)],
         modes=('normal',),strict_fault_cases=0,extra_args=modules,extra_inputs=extra,
         extra_run_args=(args.output.resolve()/'com-uniform-data.bin',),extra_sources=sources,
         extra_includes=(headers,inputs),baseline_lifetime_checks=False)
    report=json.loads((args.output/'validation.json').read_text(encoding='utf-8'))
    assert report['actual_gpu_fence_observed'] is True and report['recorded_versions']==3
    assert report['pixels_checked']==4096 and report['gpu_rendered'] is True
    assert report['geometry_from_com'] is True and report['geometry_versions']==3
    assert report['texture_from_com'] is True and report['texture_versions']==3
    assert report['texture_failed_uploads']==3 and report['texture_modified_after_recording'] is True
    assert report['depth_from_com'] is True and report['samplers_from_com'] is True
    assert report['sampler_snapshots']==3 and report['uv_outside_unit_range'] is True
    # The test must detect using the wrong sampler, not just accept creation.
    out=args.output.resolve();environment=dict(os.environ,FNV_TEST_FORCE_CLAMP='1')
    negative=subprocess.run([str(out/'com-test.exe'),str(out/'fixture.exe'),str(out/'catalog.bin'),'normal',
                             *map(str,modules),str(out/'negative-uniform-data.bin')],
                            capture_output=True,text=True,timeout=60,env=environment)
    (out/'negative-clamp.log').write_text(negative.stdout+negative.stderr)
    assert negative.returncode==1 and 'pixel frame=1' in negative.stderr
    report['wrong_clamp_detected']=True
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status='PASS',wrong_clamp_detected=True)))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','headers','uniform-fixture','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

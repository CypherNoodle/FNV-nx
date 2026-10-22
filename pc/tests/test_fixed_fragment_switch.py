"""Same-device fixed PS selection via translated COM and Windows pixel oracle."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import subprocess
from types import SimpleNamespace
from test_d3d9_com import test,SLOTS
from test_d3d9_spirv import compile_hlsl
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from translate_d3d9_pair import translate


def scalar_type(data,type_id):
    words=struct.unpack('<%dI'%(len(data)//4),data);types={};at=5
    while at<len(words):
        n,op=words[at]>>16,words[at]&65535
        if op in (22,23,32):types[words[at+1]]=(op,words[at+2:at+n])
        at+=n
    op,args=types[type_id]
    if op==32:return scalar_type(data,args[1])
    if op==23:return ('vector',args[1],scalar_type(data,args[0]))
    assert op==22;return ('float',args[0])


def run(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    compiler=Path(os.environ['SystemRoot'])/'System32/d3dcompiler_47.dll';dll=C.WinDLL(str(compiler))
    sources=[pc/n for n in ('shaders/fixed_directional_textured.vs.hlsl','tests/fixed_directional_color.ps.hlsl','shaders/fixed_directional_modulate.ps.hlsl','shaders/fixed_directional_texture_alpha.ps.hlsl')]
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    frozen={str(p):sha(p) for p in [Path(__file__).resolve(),*sources,compiler,a.linker.resolve(),Path(compile_hlsl.__code__.co_filename)]}
    for n,src in enumerate(sources):(out/('vs.bin' if not n else 'ps%d.bin'%(n-1))).write_bytes(compile_hlsl(dll,src.read_bytes(),b'vs_3_0' if not n else b'ps_3_0'))
    attributes=out/'attributes.txt';attributes.write_text('0 0 2\n3 0 2\n5 0 1\n')
    pairs=[translate(SimpleNamespace(vs=out/'vs.bin',ps=out/('ps%d.bin'%m),attributes=attributes,linker=a.linker,output=out/('pair%d'%m))) for m in range(3)]
    # One common VS module with three independently linked PS modules. Prove
    # compatible varying types/locations rather than assuming linker IDs agree.
    vs=(out/'pair1/vs.spv').read_bytes();vinfo=pairs[1]['stages']['vs'];vout={v['location']:scalar_type(vs,v['type_id']) for v in vinfo['spirv']['interfaces'] if v['storage']==3 and 'location' in v}
    for m,pair in enumerate(pairs):
        assert pair['stages']['vs']['uniform_plan']==vinfo['uniform_plan']
        assert pair['stages']['ps']['uniform_plan']['bytes']==0
        ps=(out/('pair%d/ps.spv'%m)).read_bytes()
        for v in pair['stages']['ps']['spirv']['interfaces']:
            if v['storage']==1 and 'location' in v:assert vout[v['location']]==scalar_type(ps,v['type_id'])
    inputs={v['name']:v['location'] for v in vinfo['spirv']['interfaces'] if v['storage']==1}
    plan=vinfo['uniform_plan'];assert plan['bytes']==160 and {i for e in plan['entries'] for i in range(e['first'],e['first']+e['count'])}==set(range(10))
    entries=','.join('{'+','.join(str(e[k]) for k in ('kind','first','count','offset','stride'))+'}' for e in plan['entries'])
    header='static const struct fnv_uniform_entry entries[]={'+entries+'};\nstatic const struct fnv_uniform_plan vertex_plan={160,'+str(len(plan['entries']))+',entries};\n'
    header+='static const struct fnv_vk_vertex_input vertex_inputs[]={{0,0,%d},{3,0,%d},{5,0,%d}};\n'%(inputs['vs_v0'],inputs['vs_v1'],inputs['vs_v2'])
    (out/'fixed_switch_fixture.h').write_text(header)
    shaders=out/'shaders';shaders.mkdir();records=[]
    for n in ('vs.bin','ps0.bin','ps1.bin','ps2.bin'):
        code=(out/n).read_bytes();records.append(n.encode()+bytes(256-len(n))+struct.pack('<I',len(code))+code)
    payload=b''.join(records);(shaders/'own.sdp').write_bytes(struct.pack('<III',100,4,len(payload))+payload)
    native=[pc/'native'/n for n in ('d3d9_vk_backend.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_geometry.c','d3d9_vk_state.c')]
    extras=[*[out/n for n in ('vs.bin','ps0.bin','ps1.bin','ps2.bin','pair1/vs.spv','pair0/ps.spv','pair1/ps.spv','pair2/ps.spv')],Path(__file__).resolve(),pc/'tests/directional_vertex_oracle.h',out/'fixed_switch_fixture.h',*sources,*[p for p in (pc/'native').iterdir() if p.suffix in ('.h','.inc')],*a.headers.rglob('*.h')]
    slots=SLOTS+[(107,2),(89,2),(26,7),(11,5),(12,1),(100,5),(57,3),(41,1),(42,1),(81,4),(44,3),(49,2),(51,3),(53,3),(23,9),(19,5),(20,2),(65,3),(67,4),(66,4),(108,2),(6,2),(16,7),(16,2),(60,1),(61,2),(4,1),(5,1),(13,2),(90,2)]
    opts=SimpleNamespace(sdk=a.sdk,python_deps=a.python_deps,output=out/'com',shaders=shaders,cc=a.cc)
    test(opts,host_source='fixed_fragment_switch_host.c',slots=slots,modes=('normal',),strict_fault_cases=0,extra_run_args=(out,),extra_inputs=extras,
         extra_sources=native,extra_includes=(a.headers,out),baseline_lifetime_checks=False)
    report=json.loads((out/'com/validation.json').read_text());assert report['pixel_checks']==9216 and report['synthetic_functions']==50 and report['successful_resets']==1
    controls={}
    for mode,frame in (('wrong-selector',2),('wrong-alpha',2),('wrong-stateblock',4)):
        p=subprocess.run([str(out/'com/com-test.exe'),str(out/'com/fixture.exe'),str(out/'com/catalog.bin'),mode,str(out)],capture_output=True,text=True,timeout=60)
        (out/(mode+'.log')).write_text(p.stdout+p.stderr)
        assert p.returncode and ('switch pixel mismatch frame=%d'%frame) in p.stderr
        controls[mode]=True
    assert all(sha(Path(p))==h for p,h in frozen.items())
    report.update(negative_controls=controls,shader_inputs_sha256=frozen,common_vertex_interfaces_verified=True,
                  output_sha256={str(p.relative_to(out)):sha(p) for p in [out/'vs.bin',*[out/('ps%d.bin'%m) for m in range(3)],*[out/('pair%d/translation.json'%m) for m in range(3)]]})
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS same-device COM fixed PS switch9216 pixels and3 negative controls')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','headers','linker','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

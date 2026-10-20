"""Fail-closed generation/profile/core matching before an extended CPU build."""
import argparse
import copy
import json
from pathlib import Path
import struct
import sys
from pe_fixture import write_pe


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    sys.path.insert(0,str(pc/'tools'))
    from run_aot_analysis import run,sha
    from prepare_aot_image import prepare
    from build_cpu_runtime import x87_build_inputs,SWITCH_FLAGS
    core=json.loads(a.core_validation.read_text());compiler=Path(core['compiler'][0])
    profile={'schema':'fnv-x87-protected32-v1','contract_status':'experimental','code_selector':35,'data_selector':43,'stack_selector':43,'opcode_compatibility':True,'data_pointer_on_exception_only':False}
    pp=out/'profile.json';pp.write_text(json.dumps(profile))
    exe=out/'fixture.exe';write_pe(exe);raw=bytearray(exe.read_bytes());struct.pack_into('<I',raw,0x98+224+36,0x60000020)
    code=bytes.fromhex('558becdb28db385dc3');raw[0x200:0x200+len(code)]=code;exe.write_bytes(raw)
    analysis=out/'analysis';prepare(exe,analysis,sha(exe))
    report=run(analysis,a.sdk,a.python_deps,stage='all',max_functions=8,generation_name='x87-build-contract',frame_restore=True,x87_profile=pp)
    assert not report['x87_extended_overlay']['legacy_runtime_references']
    flags,frozen,objects,digest=x87_build_inputs(report,pp,a.core_validation,compiler)
    assert flags==[*SWITCH_FLAGS,'-DFNV_CPU_EXTENDED_X87=1'] and len(objects)==len(core['objects']) and digest==sha(a.core_validation)
    assert x87_build_inputs({},None,None,compiler)==(SWITCH_FLAGS,{},[],None)
    negatives=0
    def rejected(r,p,c,cc):
        nonlocal negatives
        try:x87_build_inputs(r,p,c,cc);raise AssertionError('Incompatible build accepted')
        except ValueError:negatives+=1
    rejected({},pp,a.core_validation,compiler)
    rejected(report,None,a.core_validation,compiler);rejected(report,pp,None,compiler)
    bad=copy.deepcopy(report);bad['x87_extended_overlay']['legacy_runtime_references']={'fixture.c':1};rejected(bad,pp,a.core_validation,compiler)
    bad=copy.deepcopy(report);bad['x87_extended_overlay']['profile_sha256']='0'*64;rejected(bad,pp,a.core_validation,compiler)
    bad=copy.deepcopy(report);key=next(iter(bad['x87_extended_overlay']['source_sha256']));bad['x87_extended_overlay']['source_sha256'][key]='0'*64;rejected(bad,pp,a.core_validation,compiler)
    for index,change in enumerate(('status','switch_abi','flags','objects','duplicate','object_digest')):
        bad=copy.deepcopy(core)
        if change=='status':bad['status']='PASS'
        if change=='switch_abi':bad['switch_abi']=1
        if change=='flags':bad['flags']=['-O2']
        if change=='objects':bad['objects']=[]
        if change=='duplicate':bad['objects'].append(bad['objects'][0])
        if change=='object_digest':bad['objects'][0]['sha256']='0'*64
        path=out/f'bad-core-{index}.json';path.write_text(json.dumps(bad));rejected(report,pp,path,compiler)
    rejected(report,pp,a.core_validation,out/'wrong-compiler.exe')
    paths=[Path(__file__).resolve(),pc/'tests/pe_fixture.py',pc/'tools/build_cpu_runtime.py',pc/'tools/run_aot_analysis.py',pc/'tools/aot_x87_codegen.py',a.core_validation.resolve(),analysis/'generation-report-x87-build-contract.json']
    result=dict(status='PASS',positive_real_core_objects=len(objects),negative_build_checks=negatives,
                source_sha256={str(p):sha(p) for p in paths},engine_compiled=False,switch_hardware_tested=False)
    assert all(sha(path)==value for path,value in frozen.items())
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','core-validation','output'):p.add_argument('--'+name,type=Path,required=True)
    test(p.parse_args())

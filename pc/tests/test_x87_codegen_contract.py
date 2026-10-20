"""Synthetic profile, named-generation and mixed-model rejection contract checks."""
import argparse
import json
from pathlib import Path
import struct
import sys
from pe_fixture import write_pe


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    sys.path.insert(0,str(pc/'tools'))
    from aot_x87_codegen import load_profile
    from prepare_aot_image import prepare
    from run_aot_analysis import run,sha,sdk_identity
    profile={'schema':'fnv-x87-protected32-v1','contract_status':'experimental','code_selector':35,'data_selector':43,'stack_selector':43,'opcode_compatibility':True,'data_pointer_on_exception_only':False}
    profile_path=out/'profile.json';profile_path.write_text(json.dumps(profile));assert load_profile(profile_path)==profile
    negative=0
    for key,value in [('schema','bad'),('contract_status','verified'),('code_selector',True),('data_selector',-1),('stack_selector',65536),('opcode_compatibility',1),('data_pointer_on_exception_only',0),('extra',False)]:
        changed=dict(profile);changed[key]=value;p=out/('bad-'+key+'.json');p.write_text(json.dumps(changed))
        try:load_profile(p);raise AssertionError('Invalid profile accepted')
        except ValueError:negative+=1
    duplicate=out/'duplicate.json';duplicate.write_text(profile_path.read_text()[:-1]+',"schema":"fnv-x87-protected32-v1"}')
    try:load_profile(duplicate);raise AssertionError('Duplicate profile key accepted')
    except ValueError:negative+=1
    for name,frame,stage in [(None,True,'all'),('isolated',False,'all'),('isolated',True,'scan')]:
        try:run(out/'absent',a.sdk,a.python_deps,stage=stage,generation_name=name,frame_restore=frame,x87_profile=profile_path);raise AssertionError('Invalid overlay setup accepted')
        except ValueError:negative+=1
        assert not (out/'absent').exists()
    exe=out/'fixture.exe';write_pe(exe);raw=bytearray(exe.read_bytes());struct.pack_into('<I',raw,0x98+224+36,0x60000020)
    # FNSTSW/SAHF/SETO intentionally exposes an unsupported OF dependency; the
    # report must preserve that gap instead of calling the variant ready.
    code=bytes.fromhex('558becdb28d933dfe09e0f90c05dc3');raw[0x200:0x200+len(code)]=code;exe.write_bytes(raw)
    analysis=out/'analysis';prepare(exe,analysis,sha(exe))
    source_paths=[Path(__file__).resolve(),pc/'tests/pe_fixture.py',pc/'tools/aot_x87_codegen.py',pc/'tools/aot_x87_extended.py',pc/'tools/aot_sahf_flags.py',pc/'tools/run_aot_analysis.py']
    frozen={str(p):sha(p) for p in source_paths};sdk_hash=sdk_identity(a.sdk.resolve())
    report=run(analysis,a.sdk,a.python_deps,stage='all',max_functions=8,generation_name='x87-contract',frame_restore=True,x87_profile=profile_path)
    identity=report['x87_extended_overlay'];assert identity['profile']==profile and identity['profile_sha256']==sha(profile_path)
    assert not identity['legacy_runtime_references'] and report['status']=='GENERATED_WITH_GAPS'
    assert report['gaps']['code_markers']['explicit_unimplemented']>0
    assert not report['native_engine_started'] and not report['nro_build_ready'] and not report['cold_boot_supported']
    header=(analysis/'generated-x87-contract/recomp_funcs.h').read_text();assert header.startswith('#ifndef FNV_CPU_EXTENDED_X87\n#error')
    baseline=run(analysis,a.sdk,a.python_deps,stage='generate',max_functions=8,generation_name='legacy-contract',frame_restore=True)
    assert baseline['x87_extended_overlay'] is None
    baseline_code=''.join(p.read_text() for p in (analysis/'generated-legacy-contract').glob('*.c'))
    assert 'unsupported ten-byte x87 memory operand' in baseline_code
    assert not (analysis/'generated-legacy-contract/recomp_funcs.h').read_text().startswith('#ifndef FNV_CPU_EXTENDED_X87')
    assert all(sha(Path(p))==digest for p,digest in frozen.items()) and sdk_identity(a.sdk.resolve())==sdk_hash
    result=dict(status='PASS',negative_profile_and_setup_checks=negative,synthetic_generation_gap_preserved=True,legacy_ten_byte_guard_preserved=True,
                source_sha256=frozen,sdk_sha256=sdk_hash,generation_report_sha256=sha(analysis/'generation-report-x87-contract.json'),
                native_engine_started=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    test(p.parse_args())

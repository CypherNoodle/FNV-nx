"""Synthetic opt-in generation, real Switch objects and negative model guards."""
import argparse
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys
from pe_fixture import write_pe


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True);pc=Path(__file__).resolve().parents[1]
    sys.path[:0]=[str(pc/'tools'),str(a.python_deps.resolve()),str(a.sdk.resolve())]
    from x87_models import QUAD_MODEL,QUAD_FLAGS,QUAD_PLATFORM_SYMBOLS,TRIG_MODEL,model_flags,model_symbols
    from aot_x87_codegen import load_profile
    from run_aot_analysis import run,sha,sdk_identity
    from prepare_aot_image import prepare
    from build_cpu_runtime import x87_build_inputs,SWITCH_FLAGS,platform_symbols
    profile=dict(schema='fnv-x87-protected32-v2',contract_status='experimental',code_selector=35,data_selector=43,stack_selector=43,opcode_compatibility=True,data_pointer_on_exception_only=False,transcendental_model=QUAD_MODEL)
    pp=out/'quad.json';pp.write_text(json.dumps(profile));assert load_profile(pp)==profile
    strict={k:v for k,v in profile.items() if k!='transcendental_model'};strict['schema']='fnv-x87-protected32-v1'
    sp=out/'strict.json';sp.write_text(json.dumps(strict));assert load_profile(sp)==strict
    negatives=0
    def bad_profile(changed):
        nonlocal negatives
        path=out/f'bad-profile-{negatives}.json';path.write_text(json.dumps(changed))
        try:load_profile(path);raise AssertionError('Invalid model profile accepted')
        except ValueError:negatives+=1
    bad_profile({**strict,'transcendental_model':QUAD_MODEL})
    bad_profile({**profile,'transcendental_model':None});bad_profile({**profile,'transcendental_model':'native-bit-identical'})
    bad_profile({**profile,'transcendental_model':True});bad_profile({k:v for k,v in profile.items() if k!='transcendental_model'})
    exe=out/'fixture.exe';write_pe(exe);raw=bytearray(exe.read_bytes());struct.pack_into('<I',raw,0x98+224+36,0x60000020)
    code=bytes.fromhex('558becdb28d9f0d9f3d9f1'+('d9fed9ffd9f2d9fb' if a.trig_core else '')+'db385dc3');raw[0x200:0x200+len(code)]=code;exe.write_bytes(raw)
    analysis=out/'analysis';prepare(exe,analysis,sha(exe));sdk_hash=sdk_identity(a.sdk.resolve())
    report=run(analysis,a.sdk,a.python_deps,stage='all',max_functions=8,generation_name='quad-contract',frame_restore=True,x87_profile=pp)
    baseline=run(analysis,a.sdk,a.python_deps,stage='generate',max_functions=8,generation_name='strict-contract',frame_restore=True,x87_profile=sp)
    info=report['x87_extended_overlay'];assert info['profile']==profile and not info['legacy_runtime_references']
    generated=analysis/'generated-quad-contract';text='\n'.join(p.read_text() for p in generated.glob('*.c'))
    for op in ('F2XM1','FPATAN','FYL2X'):assert 'FNV_X87_CANDIDATE_'+op in text
    # The normal environment overlay must record each translated instruction.
    for va in (0x401005,0x401007,0x401009):assert f'instruction_pointer=0x{va:08X}u' in text
    strict_text='\n'.join(p.read_text() for p in (analysis/'generated-strict-contract').glob('*.c'))
    assert 'fnv_x87_transcendental_candidate' not in strict_text
    for op in ('f2xm1','fpatan','fyl2x'):assert op in baseline['gaps']['unsupported_instructions']
    core=json.loads(a.quad_core.read_text());compiler=Path(core['compiler'][0])
    flags,frozen,objects,digest=x87_build_inputs(report,pp,a.quad_core,compiler)
    assert flags==[*SWITCH_FLAGS,'-DFNV_CPU_EXTENDED_X87=1',*QUAD_FLAGS] and len(objects)==100
    x87_build_inputs(baseline,sp,a.strict_core,compiler)
    def rejected(r,p,c):
        nonlocal negatives
        try:x87_build_inputs(r,p,c,compiler);raise AssertionError('Mixed backend accepted')
        except ValueError:negatives+=1
    rejected(report,pp,a.strict_core);rejected(baseline,sp,a.quad_core)
    for change in ('model','object','rounding','builtin','define','contract'):
        bad=copy.deepcopy(core)
        if change=='model':bad['transcendental_model']=None
        if change=='object':bad['objects']=[x for x in bad['objects'] if Path(x['source']).name!='x87_transcendental_candidate.c']
        if change=='rounding':bad['flags'].remove('-frounding-math')
        if change=='builtin':bad['flags'].remove('-fno-builtin')
        if change=='define':bad['flags'].remove('-DFNV_CPU_X87_QUAD_MATH=1')
        if change=='contract':bad['flags'].remove('-ffp-contract=off') # Switch prefix must also reject changes.
        path=out/f'bad-core-{change}.json';path.write_text(json.dumps(bad));rejected(report,pp,path)
    assert not QUAD_PLATFORM_SYMBOLS.intersection(platform_symbols(baseline))
    assert QUAD_PLATFORM_SYMBOLS<=platform_symbols(report)
    includes=['-I'+str(p) for p in (generated,pc/'native',a.sdk.resolve()/'templates/runtime',a.sdk.resolve()/'src/kernel')]
    sources=sorted(generated.glob('recomp_[0-9][0-9][0-9][0-9].c'));assert len(sources)==1
    def compile_variant(name,source,chosen_flags,expected_error=None):
        nonlocal negatives
        cmd=[str(compiler),*chosen_flags,*includes,'-include',str(pc/'native/fnv_sdk_runtime.h'),'-c',str(source),'-o',str(out/(name+'.o'))]
        result=subprocess.run(cmd,capture_output=True,text=True);(out/(name+'.log')).write_text(result.stdout+result.stderr)
        if expected_error:
            assert result.returncode and expected_error in result.stderr;negatives+=1
        elif result.returncode:raise RuntimeError(result.stderr[-5000:])
    compile_variant('quad',sources[0],flags)
    link_rsp=out/'link.rsp';partial=out/'quad-fixture-relocatable.o'
    link_rsp.write_text('\n'.join('"'+p.as_posix()+'"' for p in [out/'quad.o',*objects])+'\n')
    link=subprocess.run([str(compiler.with_name('aarch64-none-elf-ld.exe')),'-r','-o',str(partial),'@'+str(link_rsp)],capture_output=True,text=True)
    (out/'partial-link.log').write_text(link.stdout+link.stderr)
    if link.returncode:raise RuntimeError(link.stderr)
    nm=subprocess.run([str(compiler.with_name('aarch64-none-elf-nm.exe')),'-u',str(partial)],capture_output=True,text=True)
    (out/'partial-symbols.log').write_text(nm.stdout+nm.stderr);assert nm.returncode==0
    undefined={line.split()[-1] for line in nm.stdout.splitlines() if line.strip()}
    assert 'fnv_x87_transcendental_candidate' not in undefined and QUAD_PLATFORM_SYMBOLS<=undefined
    disasm=subprocess.run([str(compiler.with_name('aarch64-none-elf-objdump.exe')),'-dr',str(out/'quad.o')],capture_output=True,text=True)
    (out/'quad-assembly.log').write_text(disasm.stdout+disasm.stderr);assert disasm.returncode==0
    assert '__aarch64_read_tp' in disasm.stdout and 'tpidr_el0' not in disasm.stdout.lower() and 'tpidr_el1' not in disasm.stdout.lower()
    compile_variant('missing-quad',sources[0],[*SWITCH_FLAGS,'-DFNV_CPU_EXTENDED_X87=1'],'Explicit bounded mathematical quad backend required')
    strict_source=next((analysis/'generated-strict-contract').glob('recomp_[0-9][0-9][0-9][0-9].c'))
    compile_variant('wrong-quad',strict_source,flags,'Strict x87 profile cannot select quad math implicitly')
    trig_details=None;trig_paths=[]
    if a.trig_core:
        tp=out/'trig.json';tp.write_text(json.dumps({**profile,'transcendental_model':TRIG_MODEL}))
        tr=run(analysis,a.sdk,a.python_deps,stage='generate',max_functions=8,generation_name='trig-contract',frame_restore=True,x87_profile=tp)
        tf,tdeps,tobjects,tdigest=x87_build_inputs(tr,tp,a.trig_core,compiler)
        assert len(tobjects)==101 and tf==[*SWITCH_FLAGS,'-DFNV_CPU_EXTENDED_X87=1',*model_flags(TRIG_MODEL)]
        ts=next((analysis/'generated-trig-contract').glob('recomp_[0-9][0-9][0-9][0-9].c'));ttext=ts.read_text()
        for op in ('FSIN','FCOS','FPTAN','FSINCOS'):assert 'FNV_X87_TRIG_'+op in ttext
        assert not tr['gaps']['unsupported_instructions'] and not tr['x87_extended_overlay']['legacy_runtime_references']
        rejected(tr,tp,a.quad_core);rejected(report,pp,a.trig_core);rejected(tr,tp,a.strict_core)
        tcore=json.loads(a.trig_core.read_text())
        for remove in ('object','define'):
            bad=copy.deepcopy(tcore)
            if remove=='object':bad['objects']=[o for o in bad['objects'] if Path(o['source']).name!='x87_trig_candidate.c']
            else:bad['flags'].remove('-DFNV_CPU_X87_QUAD_TRIG=1')
            p=out/('bad-trig-'+remove+'.json');p.write_text(json.dumps(bad));rejected(tr,tp,p)
        assert {'sinl','cosl','tanl','__divtf3'}<=platform_symbols(tr) and not {'sinl','cosl','tanl','__divtf3'}.intersection(platform_symbols(report))
        compile_variant('trig',ts,tf)
        compile_variant('missing-trig',ts,flags,'Explicit true-pi trig backend required')
        compile_variant('quad-wrong-trig',sources[0],tf,'Non-trig quad profile cannot select trig implicitly')
        compile_variant('strict-trig-only',strict_source,[*SWITCH_FLAGS,'-DFNV_CPU_EXTENDED_X87=1','-DFNV_CPU_X87_QUAD_TRIG=1'],'Strict x87 profile cannot select quad math implicitly')
        rsp=out/'trig-link.rsp';tpartial=out/'trig-fixture-relocatable.o';rsp.write_text('\n'.join('"'+p.as_posix()+'"' for p in [out/'trig.o',*tobjects])+'\n')
        lr=subprocess.run([str(compiler.with_name('aarch64-none-elf-ld.exe')),'-r','-o',str(tpartial),'@'+str(rsp)],capture_output=True,text=True);assert lr.returncode==0,lr.stderr
        nr=subprocess.run([str(compiler.with_name('aarch64-none-elf-nm.exe')),'-u',str(tpartial)],capture_output=True,text=True);assert nr.returncode==0
        undef={line.split()[-1] for line in nr.stdout.splitlines() if line.strip()}
        assert 'fnv_x87_trig_candidate' not in undef and model_symbols(TRIG_MODEL)<=undef
        assert all(sha(p)==h for p,h in tdeps.items())
        trig_paths=[a.trig_core.resolve(),tp,*map(Path,tr['x87_extended_overlay']['source_sha256'])]
        trig_details=dict(objects=101,all_seven_opcodes_emitted=True,full_backend_resolved=True,partial_sha256=sha(tpartial))
    host=subprocess.run([*a.host_cc,'-O2','-DFNV_CPU_X87_QUAD_MATH=1','-I'+str(pc/'native'),'-c',str(pc/'native/x87_transcendental_candidate.c'),'-o',str(out/'host-forbidden.o')],capture_output=True,text=True)
    (out/'host-forbidden.log').write_text(host.stdout+host.stderr);assert host.returncode and 'Selected quad model requires an ARM64 binary128 backend' in host.stderr;negatives+=1
    from aot_x87_extended import configure_transcendentals
    configure_transcendentals(QUAD_MODEL);configure_transcendentals(QUAD_MODEL)
    for model in (None,'unknown'):
        try:configure_transcendentals(model);raise AssertionError('Mixed in-process numerical model accepted')
        except ValueError:negatives+=1
    assert all(sha(p)==h for p,h in frozen.items()) and sdk_identity(a.sdk.resolve())==sdk_hash
    paths=[Path(__file__).resolve(),pc/'tests/pe_fixture.py',*map(Path,info['source_sha256']),a.quad_core.resolve(),a.strict_core.resolve(),*trig_paths]
    result=dict(status='PASS',negative_contract_checks=negatives,quad_core_objects=len(objects),real_switch_generated_unit_compiled=True,environment_pointers_tracked=True,
        numerical_model=QUAD_MODEL,trig_backend=trig_details,strict_default_preserved=True,quad_symbol_resolved_in_partial_link=True,generated_soft_tls_verified=True,
        partial_sha256=sha(partial),source_sha256={str(p):sha(p) for p in paths},sdk_sha256=sdk_hash,
        engine_compiled=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('quad-core','strict-core','sdk','python-deps','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--host-cc',nargs='+',required=True)
    parser.add_argument('--trig-core',type=Path)
    test(parser.parse_args())

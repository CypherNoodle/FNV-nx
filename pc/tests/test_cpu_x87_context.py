"""Extended x87 CPU ABI: real host threads, translated synthetic code and Switch TLS.

No game files or previous private CPU generation are used. Mixed legacy state
consumers must fail to compile or link; no binary64 projection is permitted.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
from pe_fixture import write_pe
from test_x87_environment import sha,verify


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    native=json.loads(a.build.read_text());arm=json.loads(a.arm_build.read_text());switch_core=json.loads(a.switch_build.read_text());verify(native);verify(arm);verify(switch_core)
    sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve()),str(pc/'tools')]
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    from prepare_aot_image import prepare
    from run_aot_analysis import sdk_identity
    from aot_frame_restore import install as frame
    from aot_x87_extended import install,install_environment
    from build_cpu_runtime import SWITCH_FLAGS,PLATFORM_SYMBOLS
    assert switch_core['switch_abi'] and switch_core['flags'][:len(SWITCH_FLAGS)]==SWITCH_FLAGS
    sdk_hash=sdk_identity(a.sdk.resolve())
    inputs=[Path(__file__).resolve(),pc/'tests/cpu_x87_context_host.c',pc/'tests/pe_fixture.py',pc/'tests/test_x87_environment.py',
            pc/'native/cpu_runtime.c',pc/'native/cpu_runtime.h',pc/'native/fnv_sdk_runtime.h',pc/'native/pe_image.c',pc/'native/pe_image.h',
            pc/'native/x87_stack.h',pc/'native/x87_extended.h',pc/'tools/aot_frame_restore.py',pc/'tools/aot_x87_extended.py',pc/'tools/build_cpu_runtime.py',a.build.resolve(),a.arm_build.resolve(),a.switch_build.resolve()]
    frozen={str(p.resolve()):sha(p) for p in inputs}
    exe=out/'fixture.exe';write_pe(exe);raw=bytearray(exe.read_bytes());struct.pack_into('<I',raw,0x98+224+36,0x60000020)
    programs={0x401500:bytes.fromhex('db28c3'),0x401580:bytes.fromhex('db38c3'),
              0x401600:bytes.fromhex('d9e8e8')+struct.pack('<i',0x401680-0x401607)+b'\xc3',
              0x401680:bytes.fromhex('d9ebdec1c3'),0x401700:bytes.fromhex('db28f4c3')}
    functions={}
    for va,code in programs.items():
        offset=va-0x401000+0x200;raw[offset:offset+len(code)]=code
        functions[va]=dict(start=f'0x{va:08X}',end=va+len(code),size=len(code),name=f'sub_{va:08X}',section='.text',detection_method='entry_point',called_by=[])
    exe.write_bytes(raw);prepare(exe,out/'image',sha(exe));image=out/'image/fnv-analysis.xbe';config.configure_from_xbe(str(image))
    frame();install();install_environment(code_selector=0x23,data_selector=0x2b,stack_selector=0x2b,opcode_compatibility=True,data_pointer_on_exception_only=False)
    translator=FunctionTranslator(image.read_bytes(),functions)
    code='#include "fnv_sdk_runtime.h"\n'+''.join(f'void sub_{va:08X}(void);\n' for va in functions)
    for va,entry in functions.items():code+=translator.translate_function(va,entry)+'\n'
    code+='recomp_func_t recomp_lookup(uint32_t va){switch(va){'+''.join(f'case 0x{va:08X}u:return sub_{va:08X};' for va in functions)+'default:return NULL;}}\n'
    generated=out/'generated.c';generated.write_text(code)
    includes=['-I'+str(pc/'native'),'-I'+str(a.sdk.resolve()/'templates/runtime')]
    flags=['-std=c11','-O1','-fno-strict-aliasing','-ffp-contract=off','-DFNV_CPU_EXTENDED_X87=1',*includes,'-include',str(pc/'native/fnv_sdk_runtime.h')]
    def run(command,log,success=True):
        r=subprocess.run([str(x) for x in command],capture_output=True,text=True,timeout=90);(out/log).write_text(r.stdout+r.stderr)
        if success and r.returncode:raise RuntimeError(str(log)+': '+r.stderr[-3000:])
        if not success and not r.returncode:raise AssertionError('Negative command accepted: '+str(log))
        return r
    sources=[generated,pc/'native/cpu_runtime.c',pc/'native/pe_image.c',pc/'tests/cpu_x87_context_host.c'];objects=[]
    for source in sources:
        obj=out/(source.stem+'.o');run([*native['compiler'],*flags,'-c',source,'-o',obj],source.stem+'-compiler.log');objects.append(obj)
    rsp=out/'core.rsp';rsp.write_text('\n'.join('"'+o['object'].replace('\\','/')+'"' for o in native['objects'])+'\n')
    program=out/'context-test.exe';run([*native['compiler'],*objects,'@'+str(rsp),'-o',program],'link.log')
    r=run([program,exe],'host.log');result=json.loads(r.stdout.strip().splitlines()[-1]);assert result['status']=='PASS'
    negative_checks=0
    for symbol,expression in [('g_fp_stack','g_fp_stack[0]=1'),('g_fp_top','g_fp_top=0'),('g_fp_cmp','g_fp_cmp=0'),('g_fp_cc','g_fp_cc=0'),('g_fp_control_word','g_fp_control_word=0')]:
        source=out/('forbidden-'+symbol+'.c');source.write_text('void bad(void){'+expression+';}\n')
        r=run([*native['compiler'],*flags,'-c',source,'-o',out/'forbidden.o'],source.stem+'.log',False)
        assert 'FNV_FORBIDDEN_LEGACY_X87' in r.stderr;negative_checks+=1
    legacy=out/'legacy-consumer.c';legacy.write_text('#include "cpu_runtime.h"\nint main(void){struct fnv_cpu_state s;fnv_cpu_state_init(&s,0,0);return 0;}\n')
    legacy_obj=out/'legacy-consumer.o';run([*native['compiler'],*includes,'-c',legacy,'-o',legacy_obj],'legacy-consumer-compile.log')
    r=run([*native['compiler'],legacy_obj,*objects[:-1],'@'+str(rsp),'-o',out/'mixed.exe'],'mixed-cpu-link.log',False)
    assert 'fnv_cpu_state_init' in r.stderr;negative_checks+=1
    legacy_fp=out/'legacy-fp.c';legacy_fp.write_text('#include "fnv_sdk_runtime.h"\nvoid bad(void){g_fp_stack[0]=1;}\n')
    legacy_fp_obj=out/'legacy-fp.o';run([*native['compiler'],*includes,'-c',legacy_fp,'-o',legacy_fp_obj],'legacy-fp-compile.log')
    caller=out/'caller.c';caller.write_text('void bad(void);int main(void){bad();return 0;}\n')
    r=run([*native['compiler'],caller,legacy_fp_obj,*objects[:-1],'@'+str(rsp),'-o',out/'mixed-fp.exe'],'mixed-fp-link.log',False)
    assert 'g_fp_stack' in r.stderr;negative_checks+=1
    # Compile both ends with the real Switch soft-TLS ABI, inspect relocation
    # references, and partially link without any legacy FP provider.
    switch=[];tls={}
    arm_cc=arm['compiler'];objdump=Path(arm_cc[0]).resolve().with_name('aarch64-none-elf-objdump.exe')
    for source in sources[:-1]:
        obj=out/(source.stem+'-switch.o');run([*arm_cc,*SWITCH_FLAGS,'-DFNV_CPU_EXTENDED_X87=1',*includes,'-include',pc/'native/fnv_sdk_runtime.h','-c',source,'-o',obj],source.stem+'-switch.log');switch.append(obj)
        if source.stem in ('generated','cpu_runtime'):
            assembly=run([objdump,'-dr',obj],source.stem+'-switch.asm').stdout
            tls[source.stem]=dict(soft_tls='__aarch64_read_tp' in assembly,direct_hardware_tls='tpidr_el0' in assembly or 'tpidr_el1' in assembly)
            assert tls[source.stem]['soft_tls'] and not tls[source.stem]['direct_hardware_tls']
    switch_rsp=out/'arm-core.rsp';switch_rsp.write_text('\n'.join('"'+o['object'].replace('\\','/')+'"' for o in switch_core['objects'])+'\n')
    # Link the core compiled with the actual Switch ABI too; bare oracle
    # objects are not used as Switch ABI evidence.
    partial=out/'extended-runtime.o';run([*arm_cc,'-nostdlib','-r',*switch,'@'+str(switch_rsp),'-o',partial],'switch-partial-link.log')
    nm=Path(arm_cc[0]).resolve().with_name('aarch64-none-elf-nm.exe');symbols=run([nm,partial],'switch-symbols.log').stdout
    for name in ('g_fp_stack','g_fp_top','g_fp_cmp','g_fp_cc','g_fp_control_word'):assert not any(line.split()[-1]==name for line in symbols.splitlines() if line.split())
    assert 'fnv_cpu_attach_x87e1' in symbols and 'fnv_x87_state' in symbols
    undefined={line.split()[-1] for line in symbols.splitlines() if len(line.split())==2 and line.split()[0]=='U'}
    assert not undefined-PLATFORM_SYMBOLS,sorted(undefined-PLATFORM_SYMBOLS)
    # Reuse the real CreateThread/ExitThread native bridge suite with every
    # participating unit compiled for the extended CPU ABI. Its guest worker
    # programs are integer-only; x87 payload concurrency is tested above.
    from types import SimpleNamespace
    from test_windows_workers import test_workers
    test_workers(SimpleNamespace(sdk=a.sdk,python_deps=a.python_deps,output=out/'managed-workers',cc=[*native['compiler'],'-DFNV_CPU_EXTENDED_X87=1']),
                 extra_inputs=(Path(__file__).resolve(),pc/'native/x87_stack.h',pc/'native/x87_extended.h'))
    worker_proof=out/'managed-workers/validation.json';workers=json.loads(worker_proof.read_text());assert workers['status']=='PASS'
    for name,digest in workers['source_sha256'].items():assert sha(Path(name))==digest
    assert sdk_identity(a.sdk.resolve())==sdk_hash and all(sha(Path(p))==digest for p,digest in frozen.items());verify(native);verify(arm);verify(switch_core)
    result.update(negative_mixed_model_checks=negative_checks,synthetic_functions=len(functions),switch_tls=tls,
        managed_workers_validation_sha256=sha(worker_proof),managed_workers=workers['managed_threads'],managed_worker_strict_stops=workers['strict_flag_stops'],
        switch_partial_link_sha256=sha(partial),unexpected_switch_symbols=[],source_sha256=frozen,sdk_sha256=sdk_hash,
        generated_synthetic_sha256=sha(generated),engine_generation_migrated=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','arm-build','switch-build','sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    test(p.parse_args())

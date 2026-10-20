"""Actual native SAHF vs translated SETcc/Jcc/CMOVcc and explicit OF rejection."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
from pe_fixture import write_pe


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve()),str(pc/'tools')]
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    from prepare_aot_image import prepare
    from run_aot_analysis import sha,sdk_identity
    from aot_frame_restore import install as frame
    from aot_x87_extended import install as extended
    from aot_sahf_flags import install
    from build_cpu_runtime import SWITCH_FLAGS
    frame();extended();install()
    exe=out/'fixture.exe';write_pe(exe);raw=bytearray(exe.read_bytes());struct.pack_into('<I',raw,0x98+224+36,0x60000020)
    prefix=bytes.fromhex('8b019eb800000000')
    stores=b''.join(bytes([0x0f,opcode,0x42,index]) for index,opcode in enumerate((0x92,0x93,0x96,0x97,0x94,0x95,0x9a,0x9b,0x98,0x99)))
    programs={0x401500:prefix+stores+b'\xc3',
              0x401580:prefix+bytes.fromhex('7206b801000000c3b802000000c3'),
              0x401600:bytes.fromhex('8b019ebb02000000b8010000000f44c3c3'),
              0x401680:bytes.fromhex('8b019e0f9002c3'),
              0x401700:prefix+bytes.fromhex('83d000c3'),0x401780:prefix+bytes.fromhex('83d800c3')}
    functions={}
    for va,code in programs.items():
        offset=va-0x401000+0x200;raw[offset:offset+len(code)]=code
        functions[va]=dict(start=f'0x{va:08X}',end=va+len(code),size=len(code),name=f'sub_{va:08X}',section='.text',detection_method='entry_point',called_by=[])
    exe.write_bytes(raw);prepare(exe,out/'image',sha(exe));image=out/'image/fnv-analysis.xbe';config.configure_from_xbe(str(image));translator=FunctionTranslator(image.read_bytes(),functions)
    source='#include "fnv_sdk_runtime.h"\n'+''.join(f'void sub_{va:08X}(void);\n' for va in functions)
    for va,info in functions.items():source+=translator.translate_function(va,info)+'\n'
    source+='recomp_func_t recomp_lookup(uint32_t va){switch(va){'+''.join(f'case 0x{va:08X}:return sub_{va:08X};' for va in functions)+'default:return NULL;}}\n'
    generated=out/'generated.c';generated.write_text(source)
    host=out/'host.c';host.write_text(r'''#undef NDEBUG
#include "cpu_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv){assert(argc==2);unsigned char *arena=malloc(0x100000);assert(arena);struct fnv_native_image image;assert(fnv_native_load_image(argv[1],arena,0x100000,&image)==FNV_NATIVE_OK);
struct fnv_cpu_config config={.memory=arena,.guest_base=0x400000,.memory_bytes=0x100000,.code_low=0x401500,.code_high=0x401800};struct fnv_cpu_runtime *cpu;assert(!fnv_cpu_create(&config,&cpu));
struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;assert(!fnv_cpu_bind_imports(cpu,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count));
struct fnv_cpu_state s,got;fnv_cpu_state_init(&s,0x4ff000,0);s.gpr[1]=0x402000;s.gpr[2]=0x402100;assert(!fnv_cpu_attach(cpu,&s));
for(unsigned flags=0;flags<256;flags++){
uint32_t word=0x12340056u|flags<<8;memcpy(arena+0x2000,&word,4);unsigned char expected[10];uint32_t ax=word;
__asm__ volatile("sahf;setb 0(%1);setae 1(%1);setbe 2(%1);seta 3(%1);sete 4(%1);setne 5(%1);setp 6(%1);setnp 7(%1);sets 8(%1);setns 9(%1)":"+a"(ax):"r"(expected):"cc","memory");
memset(arena+0x2100,0xa5,12);assert(!fnv_cpu_set_state(&s));assert(!fnv_cpu_execute(0x401500,NULL));assert(!memcmp(arena+0x2100,expected,10) && arena[0x210a]==0xa5 && arena[0x210b]==0xa5);
assert(!fnv_cpu_set_state(&s));assert(!fnv_cpu_execute(0x401580,NULL));assert(!fnv_cpu_get_state(&got));assert(got.gpr[0]==((flags&1)?2u:1u));
assert(!fnv_cpu_set_state(&s));assert(!fnv_cpu_execute(0x401600,NULL));assert(!fnv_cpu_get_state(&got));assert(got.gpr[0]==((flags&0x40)?2u:1u));
assert(!fnv_cpu_set_state(&s));assert(!fnv_cpu_execute(0x401700,NULL));assert(!fnv_cpu_get_state(&got));assert(got.gpr[0]==expected[0]);
assert(!fnv_cpu_set_state(&s));assert(!fnv_cpu_execute(0x401780,NULL));assert(!fnv_cpu_get_state(&got));assert(got.gpr[0]==0u-(uint32_t)expected[0]);
}
memset(arena+0x2100,0xa5,12);assert(!fnv_cpu_set_state(&s));struct fnv_cpu_fault fault;assert(fnv_cpu_execute(0x401680,&fault)==FNV_CPU_GUEST_FAULT);assert(fault.kind==FNV_CPU_UNIMPLEMENTED && fault.address==0x401682 && arena[0x2100]==0xa5);
assert(!fnv_cpu_detach());assert(!fnv_cpu_destroy(cpu));free(arena);printf("PASS 256 native SAHF records, 2560 SETcc, 256 Jcc, 256 CMOVcc plus ADC/SBB and explicit OF stop\n");return 0;}
''')
    inputs=[Path(__file__).resolve(),pc/'tests/pe_fixture.py',pc/'tools/aot_sahf_flags.py',pc/'tools/aot_x87_extended.py',pc/'tools/aot_frame_restore.py',pc/'native/cpu_runtime.c',pc/'native/cpu_runtime.h',pc/'native/fnv_sdk_runtime.h',pc/'native/x87_stack.h',pc/'native/x87_extended.h',pc/'native/pe_image.c',pc/'native/pe_image.h',pc/'tools/run_aot_analysis.py',pc/'tools/build_cpu_runtime.py']
    frozen={str(p.resolve()):sha(p) for p in inputs};sdk_hash=sdk_identity(a.sdk.resolve())
    includes=['-I'+str(pc/'native'),'-I'+str(a.sdk.resolve()/'templates/runtime')]
    flags=['-DFNV_CPU_EXTENDED_X87=1','-include',str(pc/'native/fnv_sdk_runtime.h'),*includes]
    program=out/'sahf-test.exe'
    r=subprocess.run([*a.cc,'-O1',*flags,str(generated),str(host),str(pc/'native/cpu_runtime.c'),str(pc/'native/pe_image.c'),'-o',str(program)],capture_output=True,text=True);(out/'compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr[-3000:])
    r=subprocess.run([str(program),str(exe)],capture_output=True,text=True,timeout=30);(out/'host.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    obj=out/'generated-switch.o';r=subprocess.run([str(a.arm_cc),*SWITCH_FLAGS,*flags,'-c',str(generated),'-o',str(obj)],capture_output=True,text=True);(out/'switch-compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr[-3000:])
    assert all(sha(Path(path))==digest for path,digest in frozen.items()) and sdk_identity(a.sdk.resolve())==sdk_hash
    result=dict(status='PASS',native_sahf_cases=256,setcc_results=2560,jcc_results=256,cmovcc_results=256,adc_results=256,sbb_results=256,overflow_consumer_stops=True,
                generated_synthetic_sha256=sha(generated),switch_compile_sha256=sha(obj),source_sha256=frozen,sdk_sha256=sdk_hash,
                limitations=['Local supported flag snapshot, not full cross-call EFLAGS','AF reconstruction by SDK LAHF and preserved OF are not implemented'],engine_generation_migrated=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(r.stdout or 'PASS native SAHF differential and Switch compilation')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output','arm-cc'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);test(p.parse_args())

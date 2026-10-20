"""FCOMI/FUCOMI: native flags, real translated consumers, and ARM64 state."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
from pe_fixture import write_pe
from test_x87_integer_compare import sha,verify


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True);pc=Path(__file__).resolve().parents[1]
    native=json.loads(a.build.read_text());arm_proof=json.loads(a.arm_build.read_text());verify(native);verify(arm_proof)
    sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve()),str(pc/'tools')]
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    import tools.recomp.lifter as module
    from tools.recomp.disasm import Disassembler
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    from prepare_aot_image import prepare
    from aot_frame_restore import install as frame
    from aot_x87_extended import install as extended
    from aot_sahf_flags import install as flags
    from run_aot_analysis import sdk_identity
    from build_cpu_runtime import SWITCH_FLAGS
    from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
    import unicorn.arm64_const as ar
    frame();extended();flags();decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True;disassembler=Disassembler()
    mnemonics=('fcomi','fucomi','fcomip','fucomip');opcodes=((0xdb,0xf0),(0xdb,0xe8),(0xdf,0xf0),(0xdf,0xe8))
    conditions=('jo','jno','jb','jae','je','jne','jbe','ja','js','jns','jp','jnp','jl','jge','jle','jg')
    native_cases=[];arm_cases=[];programs={};functions={}
    stores=b''.join(bytes([0x0f,0x90+i,0x42,i]) for i in range(16))
    for mode,(opcode,reg) in enumerate(opcodes):
        for index in range(8):
            form=mode*8+index;raw=bytes([opcode,reg+index]);base=0x401000+form*0x100
            programs[base]=raw+stores+b'\xc3'
            programs[base+0x80]=raw+bytes.fromhex('b80000000083d000c3')
            programs[base+0xa0]=raw+bytes.fromhex('b80000000083d800c3')
            programs[base+0xc0]=raw+bytes.fromhex('b801000000bb020000000f42c3c3')
            programs[base+0xe0]=raw+bytes.fromhex('7a06b801000000c3b802000000c3')
            loads='; '.join('fldt %'+str(i&1) for i in range(7,-1,-1))
            setters='; '.join('set'+name[1:]+' '+str(88+i)+'(%%rdi)' for i,name in enumerate(conditions))
            dumps='; '.join('fstpt '+str(i*10)+'(%%rdi)' for i in range(7 if mode>=2 else 8))
            # Seed condition bits after loading: FLD itself clears C1.
            native_cases.append(f'case {form}: __asm__ volatile("'+loads+'; fnstenv %2" : : "m"(*(const unsigned char (*)[10])input),"m"(*(const unsigned char (*)[10])(input+10)),"m"(*(unsigned char (*)[28])environment) : "memory","st","st(1)","st(2)","st(3)","st(4)","st(5)","st(6)","st(7)");'
                +'environment[5]=(unsigned char)((environment[5]&~0x47u)|(input[25]&0x47u));'
                +'__asm__ volatile("fldenv %0; mov $0xd77f,%%eax; addb $1,%%al; sahf; .byte '+str(opcode)+','+str(reg+index)+'; '+setters+'; pushfq; popq %%rax; movl %%eax,84(%%rdi); fnstsw 80(%%rdi); '+dumps+'" : : "m"(*(const unsigned char (*)[28])environment),"D"(out) : "memory","eax","cc","st","st(1)","st(2)","st(3)","st(4)","st(5)","st(6)","st(7)");break;')
            lifter=module.Lifter();ins=disassembler._decode_instruction(next(decoder.disasm(raw,0x400000+form*16)))
            code=lifter.lift_instruction(ins);assert not lifter.unimplemented
            predicates=[module._make_condition(name,ins.mnemonic,[])[0] for name in conditions]
            assert all(p is not None and 'g_fp_cmp' not in p for p in predicates)
            arm_cases.append(f'case {form}: '+'{\n'+'\n'.join(code)+'\n'+'\n'.join(f'out[{88+i}]=!!({p});' for i,p in enumerate(predicates))+'\nbreak;}')
    exe_image=out/'fixture.exe';write_pe(exe_image);raw=bytearray(exe_image.read_bytes());raw.extend(bytes(0x2200-len(raw)))
    struct.pack_into('<I',raw,0x98+56,0x4000);struct.pack_into('<I',raw,0x98+224+8,0x2000);struct.pack_into('<I',raw,0x98+224+16,0x2000);struct.pack_into('<I',raw,0x98+224+36,0x60000020)
    for va,code in programs.items():
        raw[va-0x401000+0x200:va-0x401000+0x200+len(code)]=code
        functions[va]=dict(start=f'0x{va:08X}',end=va+len(code),size=len(code),name=f'sub_{va:08X}',section='.text',detection_method='entry_point',called_by=[])
    exe_image.write_bytes(raw);prepare(exe_image,out/'image',sha(exe_image));image=out/'image/fnv-analysis.xbe';config.configure_from_xbe(str(image));translator=FunctionTranslator(image.read_bytes(),functions)
    generated=out/'generated.c';generated.write_text('#include "fnv_sdk_runtime.h"\n'+''.join(f'void sub_{va:08X}(void);\n' for va in functions)+''.join(translator.translate_function(va,info)+'\n' for va,info in functions.items())+'recomp_func_t recomp_lookup(uint32_t va){switch(va){'+''.join(f'case 0x{va:08X}:return sub_{va:08X};' for va in functions)+'default:return NULL;}}\n')
    assert 'g_fp_cmp' not in generated.read_text()
    host=out/'host.c';host.write_text('''#undef NDEBUG
#include "cpu_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
static void snapshot(const struct fnv_x87_stack *s,uint32_t flags,unsigned char out[104]) {
for(unsigned i=0;i<((s->occupied==0xff)?8:7);i++)fnv_x87_pack(out+i*10,s->registers[(s->top+i)&7]);
out[80]=(unsigned char)(s->status&0x3f);out[81]=(unsigned char)((s->status&0x7f00)>>8);out[82]=s->top;out[83]=s->occupied;memcpy(out+84,&flags,4);
}
static void oracle(const unsigned char input[28],unsigned char out[104]) {
uint16_t old,cw=(uint16_t)(input[20]|input[21]<<8);unsigned char environment[28];memset(out,0,104);
__asm__ volatile("fnstcw %0" : "=m"(old));__asm__ volatile("fninit; fldcw %0" : : "m"(cw));
switch(input[22]*8+input[23]) {
'''+ '\n'.join(native_cases)+'''
}
uint16_t sw=(uint16_t)(out[80]|out[81]<<8);sw&=0x7f3f;out[80]=(unsigned char)sw;out[81]=(unsigned char)(sw>>8);out[82]=(unsigned char)((sw>>11)&7);out[83]=input[22]>=2?0xfe:0xff;
uint32_t bits;memcpy(&bits,out+84,4);bits&=0x8d5;memcpy(out+84,&bits,4);
__asm__ volatile("fnclex; fldcw %0" : : "m"(old));
}
int main(int argc,char **argv) {
assert(argc==2);assert(_setmode(_fileno(stdin),_O_BINARY)>=0 && _setmode(_fileno(stdout),_O_BINARY)>=0);
unsigned char *arena=malloc(0x100000);assert(arena);struct fnv_native_image image;assert(!fnv_native_load_image(argv[1],arena,0x100000,&image));
struct fnv_cpu_config config={.memory=arena,.guest_base=0x400000,.memory_bytes=0x100000,.code_low=0x401000,.code_high=0x403000};struct fnv_cpu_runtime *cpu;assert(!fnv_cpu_create(&config,&cpu));
struct fnv_native_import imports[FNV_NATIVE_MAX_IMPORTS];size_t count;assert(!fnv_cpu_bind_imports(cpu,&image,imports,FNV_NATIVE_MAX_IMPORTS,NULL,0,&count));
struct fnv_cpu_state initial,got;fnv_cpu_state_init(&initial,0x4ff000,0);initial.gpr[2]=0x404000;assert(!fnv_cpu_attach(cpu,&initial));
unsigned char input[28];unsigned records=0;
for(;;){size_t n=fread(input,1,28,stdin);if(!n)break;if(n!=28 || input[22]>3 || input[23]>7)return 2;
unsigned char expected[104],actual[104]={0};oracle(input,expected);struct fnv_x87_stack state;fnv_x87_stack_init(&state);state.control=(uint16_t)(input[20]|input[21]<<8);
for(int i=7;i>=0;i--)assert(fnv_x87_stack_load(&state,input+((i&1)?10:0))==FNV_X87_STACK_OK);
state.status=(uint16_t)(input[24]|input[25]<<8);struct fnv_x87_stack before=state;uint32_t flags=0xffffffff;
assert(fnv_x87_stack_compare_flags(&state,input[23],input[22]&1,input[22]>=2,&flags)==FNV_X87_STACK_OK);snapshot(&state,flags,actual);
if(memcmp(actual,expected,88)){fprintf(stderr,"core mismatch %u form %u index %u status %04x/%04x\\n",records,input[22],input[23],state.status,expected[80]|expected[81]<<8);for(unsigned i=0;i<88;i++)fprintf(stderr,"%02x",actual[i]);fputc('\\n',stderr);for(unsigned i=0;i<88;i++)fprintf(stderr,"%02x",expected[i]);fputc('\\n',stderr);return 3;}
uint32_t base=0x401000+(input[22]*8+input[23])*0x100;initial.x87_extended=before;
memset(arena+0x4000,0xa5,20);assert(!fnv_cpu_set_state(&initial));assert(!fnv_cpu_execute(base,NULL));assert(!memcmp(arena+0x4000,expected+88,16) && arena[0x4010]==0xa5);assert(!fnv_cpu_get_state(&got));
unsigned char got_snapshot[104]={0};snapshot(&got.x87_extended,flags,got_snapshot);assert(!memcmp(got_snapshot,expected,88));
const uint32_t offsets[]={0x80,0xa0,0xc0,0xe0};uint32_t cf=flags&1u;uint32_t values[]={cf,0u-cf,cf?2u:1u,(flags&4)?2u:1u};
for(unsigned i=0;i<4;i++){assert(!fnv_cpu_set_state(&initial));assert(!fnv_cpu_execute(base+offsets[i],NULL));assert(!fnv_cpu_get_state(&got));if(got.gpr[0]!=values[i]){fprintf(stderr,"consumer mismatch %u kind %u got %u expected %u\\n",records,i,got.gpr[0],values[i]);return 4;}}
if(fwrite(expected,1,104,stdout)!=104)return 5;records++;}
assert(!fnv_cpu_detach());assert(!fnv_cpu_destroy(cpu));free(arena);fprintf(stderr,"PASS %u FCOMI-family core/caller cases\\n",records);return ferror(stdin)?6:0;
}
''')
    def response(proof,name):
        path=out/name;path.write_text('\n'.join('"'+e['object'].replace('\\','/')+'"' for e in proof['objects'])+'\n');return path
    def compile(command,log):
        r=subprocess.run(command,capture_output=True,text=True);(out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr[-3000:] or str(r.returncode))
    includes=['-I'+str(pc/'native'),'-I'+str(a.sdk.resolve()/'templates/runtime')]
    exe=out/'oracle.exe';compile([*native['compiler'],'-O1','-DFNV_CPU_EXTENDED_X87=1','-include',str(pc/'native/fnv_sdk_runtime.h'),*includes,str(generated),str(host),str(pc/'native/cpu_runtime.c'),str(pc/'native/pe_image.c'),'@'+str(response(native,'native.rsp')),'-Wl,--gc-sections','-o',str(exe)],'native-compiler.log')
    def raw(sig,exp):return struct.pack('<QH',sig,exp)
    one=raw(1<<63,0x3fff);negative=raw(1<<63,0xbfff);zero=bytes(10);nan=raw(0xc000000000001234,0x7fff);inf=raw(1<<63,0x7fff)
    pairs=[(one,negative),(negative,one),(one,one),(zero,raw(0,0x8000)),(inf,one),(one,inf),(nan,one),(one,nan),(nan,nan),(raw((1<<63)+1,0x3fff),one)]
    records=[x+y+struct.pack('<HBBHH',0x7f|(precision<<8)|(rounding<<10),mode,index,cc,0) for mode in range(4) for index in range(8) for precision in (0,2,3) for rounding in range(4) for cc in (0,0x4700) for x,y in pairs]
    data=b''.join(records);(out/'inputs.bin').write_bytes(data);r=subprocess.run([str(exe),str(exe_image)],input=data,capture_output=True);(out/'oracle.log').write_bytes(r.stderr)
    if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace'))
    expected=r.stdout;assert len(expected)==len(records)*104;(out/'expected.bin').write_bytes(expected)
    entry=out/'entry.c';entry.write_text('''#include "x87_stack.h"
__attribute__((section(".text.entry"))) void probe(const unsigned char *request,unsigned char *out) {
struct fnv_x87_stack fnv_x87_state;fnv_x87_stack_init(&fnv_x87_state);fnv_x87_state.control=(uint16_t)(request[20]|request[21]<<8);
for(int i=7;i>=0;i--)if(fnv_x87_stack_load(&fnv_x87_state,request+((i&1)?10:0))!=FNV_X87_STACK_OK){out[0]=255;return;}
fnv_x87_state.status=(uint16_t)(request[24]|request[25]<<8);uint32_t _fnv_x87_flags=0xffffffffu;
#define RECOMP_UNIMPL(text,va) do {out[0]=254;return;} while(0)
switch(request[22]*8+request[23]) {
'''+ '\n'.join(arm_cases)+'''
}
for(unsigned i=0;i<80;i++)out[i]=0;
for(unsigned i=0;i<(request[22]>=2?7u:8u);i++)fnv_x87_pack(out+i*10,fnv_x87_state.registers[(fnv_x87_state.top+i)&7]);
out[80]=(unsigned char)(fnv_x87_state.status&0x3f);out[81]=(unsigned char)((fnv_x87_state.status&0x7f00)>>8);out[82]=fnv_x87_state.top;out[83]=fnv_x87_state.occupied;
for(unsigned i=0;i<4;i++)out[84+i]=(unsigned char)(_fnv_x87_flags>>(8*i));
}
''')
    layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    elf=out/'probe.elf';cc=arm_proof['compiler'];compile([*cc,'-O2','-ffreestanding','-fno-stack-protector','-nostdlib','-I'+str(pc/'native'),str(entry),'@'+str(response(arm_proof,'arm.rsp')),'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-o',str(elf)],'arm-compiler.log')
    binary=out/'probe.bin';subprocess.run([str(Path(cc[0]).resolve().with_name('aarch64-none-elf-objcopy.exe')),'-O','binary',str(elf),str(binary)],check=True)
    arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x200000);arm.mem_write(0x100000,binary.read_bytes());arm.mem_map(0x800000,0x10000)
    for index,record in enumerate(records):
        arm.mem_write(0x800100,record);arm.mem_write(0x800200,b'\xa5'*108)
        arm.reg_write(ar.UC_ARM64_REG_X0,0x800100);arm.reg_write(ar.UC_ARM64_REG_X1,0x800200);arm.reg_write(ar.UC_ARM64_REG_SP,0x80f000);arm.reg_write(ar.UC_ARM64_REG_LR,0x2ff000)
        arm.emu_start(0x100000,0x2ff000,count=20000)
        assert arm.reg_read(ar.UC_ARM64_REG_PC)==0x2ff000,index
        assert bytes(arm.mem_read(0x800200,108))==expected[index*104:index*104+104]+b'\xa5'*4,index
    switch_obj=out/'generated-switch.o'
    compile([*cc,*SWITCH_FLAGS,'-DFNV_CPU_EXTENDED_X87=1','-include',str(pc/'native/fnv_sdk_runtime.h'),*includes,'-c',str(generated),'-o',str(switch_obj)],'switch-compiler.log')
    verify(native);verify(arm_proof)
    paths=[Path(__file__).resolve(),Path(__file__).with_name('test_x87_integer_compare.py'),Path(__file__).with_name('pe_fixture.py'),a.build.resolve(),a.arm_build.resolve(),pc/'tools/aot_x87_extended.py',pc/'tools/aot_sahf_flags.py',pc/'tools/aot_frame_restore.py',pc/'tools/build_cpu_runtime.py',pc/'native/cpu_runtime.c',pc/'native/cpu_runtime.h',pc/'native/fnv_sdk_runtime.h',pc/'native/pe_image.c',pc/'native/pe_image.h']
    result=dict(status='PASS',native_cases=len(records),arm64_translated_cases=len(records),setcc_results=16*len(records),translated_caller_executions=5*len(records),opcode_forms=32,c1_initial_states=[0,1],source_sha256={str(p):sha(p) for p in paths},sdk_sha256=sdk_identity(a.sdk.resolve()),switch_hardware_tested=False,
        native_assertions_enabled=True,switch_compile_sha256=sha(switch_obj),
        limits=['Masked canonical source operands; local flag consumers only; full cross-call EFLAGS, LAHF/PUSHF and guest exception delivery remain unsupported'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('build','arm-build','sdk','python-deps','output'):parser.add_argument('--'+name,type=Path,required=True)
    test(parser.parse_args())

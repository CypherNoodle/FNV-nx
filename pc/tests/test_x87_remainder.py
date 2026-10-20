"""FPREM/FPREM1 iterative reduction and quotient flags versus native x87."""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
from test_x87_integer_compare import sha,verify


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True);pc=Path(__file__).resolve().parents[1]
    native=json.loads(a.build.read_text());arm_proof=json.loads(a.arm_build.read_text());verify(native);verify(arm_proof)
    sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve()),str(pc/'tools')]
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    from tools.recomp.disasm import Disassembler
    from tools.recomp.lifter import Lifter
    from aot_frame_restore import install as frame
    from aot_x87_extended import install as extended
    from run_aot_analysis import sdk_identity
    from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
    import unicorn.arm64_const as ar
    frame();extended();decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True;disassembler=Disassembler()
    def lift(raw):
        lifter=Lifter();code=[]
        for decoded in decoder.disasm(raw,0x400000):code+=lifter.lift_instruction(disassembler._decode_instruction(decoded))
        assert not lifter.unimplemented;return '\n'.join(code)
    loads=lift(bytes.fromhex('db2d10002000db2d00002000'));ops=[lift(bytes.fromhex(raw)) for raw in ('d9f8','d9f5')]
    native_cases=[]
    for index,mnemonic in enumerate(('fprem','fprem1')):
        text='fldt %1; fldt %0; xor %%ecx,%%ecx; 1: inc %%ecx; '+mnemonic+'; fnstsw %%ax; cmp $1,%%ecx; jne 3f; mov %%ax,24(%%rdi); 3: test $0x400,%%ax; jz 2f; cmp $2048,%%ecx; jb 1b; 2: mov %%ax,20(%%rdi); fstpt (%%rdi); fstpt 10(%%rdi)'
        native_cases.append(f'case {index}: __asm__ volatile("'+text+'" : : "m"(*(const unsigned char (*)[10])input),"m"(*(const unsigned char (*)[10])(input+10)),"D"(out) : "memory","eax","ecx","cc","st","st(1)");break;')
    source=out/'oracle.c';source.write_text('''#include "x87_stack.h"
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
static void oracle(const unsigned char input[24],unsigned char out[28]) {
uint16_t old,cw=(uint16_t)(input[20]|input[21]<<8),sw,first;memset(out,0,28);
__asm__ volatile("fnstcw %0" : "=m"(old));__asm__ volatile("fninit; fldcw %0" : : "m"(cw));
switch(input[22]) {
'''+ '\n'.join(native_cases)+'''
}
sw=(uint16_t)(out[20]|out[21]<<8);sw&=0x7f3f;out[20]=(unsigned char)sw;out[21]=(unsigned char)(sw>>8);
first=(uint16_t)(out[24]|out[25]<<8);out[24]=(first&0x400)?1:0;out[25]=out[26]=out[27]=0;out[22]=0xc0;out[23]=6;
__asm__ volatile("fnclex; fldcw %0" : : "m"(old));
}
int main(void) {
if(_setmode(_fileno(stdin),_O_BINARY)<0 || _setmode(_fileno(stdout),_O_BINARY)<0)return 1;
unsigned char input[24];unsigned count=0;
for(;;){size_t n=fread(input,1,24,stdin);if(!n)break;if(n!=24 || input[22]>1)return 2;
unsigned char expected[28],actual[28]={0};oracle(input,expected);
struct fnv_x87_stack s;fnv_x87_stack_init(&s);s.control=(uint16_t)(input[20]|input[21]<<8);
if(fnv_x87_stack_load(&s,input+10)!=FNV_X87_STACK_OK || fnv_x87_stack_load(&s,input)!=FNV_X87_STACK_OK)return 3;
unsigned iterations=0;do {if(fnv_x87_stack_remainder(&s,input[22])!=FNV_X87_STACK_OK)return 4;
if(++iterations==1)actual[24]=(s.status&0x400)?1:0;if(iterations>2048)return 8;}while(s.status&0x400);
fnv_x87_pack(actual,s.registers[s.top]);fnv_x87_pack(actual+10,s.registers[(s.top+1)&7]);
actual[20]=(unsigned char)(s.status&0x3f);actual[21]=(unsigned char)((s.status&0x7f00)>>8);actual[22]=s.occupied;actual[23]=s.top;
if(memcmp(actual,expected,28)){fprintf(stderr,"Mismatch case %u cw=%04x mode=%u iterations=%u\\n",count,s.control,input[22],iterations);for(unsigned i=0;i<28;i++)fprintf(stderr,"%02x",actual[i]);fputc('\\n',stderr);for(unsigned i=0;i<28;i++)fprintf(stderr,"%02x",expected[i]);fputc('\\n',stderr);return 5;}
if(fwrite(expected,1,28,stdout)!=28)return 6;count++;}fprintf(stderr,"PASS %u native remainder cases\\n",count);return ferror(stdin)?7:0;
}
''')
    def response(proof,name):
        path=out/name;path.write_text('\n'.join('"'+e['object'].replace('\\','/')+'"' for e in proof['objects'])+'\n');return path
    def compile(command,log):
        r=subprocess.run(command,capture_output=True,text=True);(out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr[-3000:] or str(r.returncode))
    exe=out/'oracle.exe';compile([*native['compiler'],'-O2','-I'+str(pc/'native'),str(source),'@'+str(response(native,'native.rsp')),'-Wl,--gc-sections','-o',str(exe)],'native-compiler.log')
    def raw(sig,exp):return struct.pack('<QH',sig,exp)
    def integer(n):
        if not n:return bytes(10)
        bits=abs(n).bit_length();return raw(abs(n)<<(64-bits),(0x8000 if n<0 else 0)|((bits-1)+0x3fff))
    pairs=[(integer(a),integer(b)) for a in (0,1,-1,5,-5,7,-7,11,-11,13,-13) for b in (0,2,-2,3,-3)]
    pairs.extend((raw(sa,ea),raw(sb,eb)) for sa,ea,sb,eb in [
        (0,0x8000,1<<63,0x3fff),(1<<63,0x7fff,1<<63,0x3fff),
        (1<<63,0x3fff,1<<63,0x7fff),(0xc000000000001234,0x7fff,1<<63,0x3fff),
        (1<<63,0x3fff,0xc000000000005678,0x7fff),
        (0xffffffffffffffff,1,0x8000000000000001,1),
        (0xffffffffffffffff,0x7ffe,0x8000000000003039,1)])
    for difference in (-2,-1,0,1,2,31,32,63,64,65,95,96,127,128,129):
        for sign in (0,0x8000):pairs.append((raw(0xfedcba9876543210,(0x4000+difference)|sign),raw(0x8123456789abcdef,0x4000)))
    rng=random.Random(0xf9e1)
    for _ in range(96):
        be=rng.randint(100,32000);difference=rng.randint(-20,200)
        pairs.append((raw(rng.getrandbits(63)|(1<<63),(be+difference)|(rng.randrange(2)<<15)),raw(rng.getrandbits(63)|(1<<63),be|(rng.randrange(2)<<15))))
    records=[v+s+struct.pack('<HBB',0x7f|(precision<<8)|(rounding<<10),mode,0) for mode in range(2) for precision in (0,2,3) for rounding in range(4) for v,s in pairs]
    (out/'inputs.bin').write_bytes(b''.join(records));r=subprocess.run([str(exe)],input=b''.join(records),capture_output=True);(out/'oracle.log').write_bytes(r.stderr)
    if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace'))
    expected=r.stdout;assert len(expected)==len(records)*28;(out/'expected.bin').write_bytes(expected)
    entry=out/'entry.c';entry.write_text('''#include <stddef.h>
#include "x87_stack.h"
__attribute__((section(".text.entry"))) void probe(const unsigned char *request,unsigned char *out) {
struct fnv_x87_stack fnv_x87_state;fnv_x87_stack_init(&fnv_x87_state);fnv_x87_state.control=(uint16_t)(request[20]|request[21]<<8);
unsigned char guest[32]={0};for(unsigned i=0;i<10;i++){guest[i]=request[i];guest[16+i]=request[10+i];}
ptrdiff_t g_xbox_mem_offset=(ptrdiff_t)guest-0x200000;
#define RECOMP_UNIMPL(text,va) do {out[0]=255;return;} while(0)
'''+loads+'''
unsigned iterations=0;do {
if(request[22]) {'''+ops[1]+'''} else {'''+ops[0]+'''}
if(++iterations==1)out[24]=(fnv_x87_state.status&0x400)?1:0;if(iterations>2048){out[0]=254;return;}
}while(fnv_x87_state.status&0x400);
fnv_x87_pack(out,fnv_x87_state.registers[fnv_x87_state.top]);fnv_x87_pack(out+10,fnv_x87_state.registers[(fnv_x87_state.top+1)&7]);
out[20]=(unsigned char)(fnv_x87_state.status&0x3f);out[21]=(unsigned char)((fnv_x87_state.status&0x7f00)>>8);out[22]=fnv_x87_state.occupied;out[23]=fnv_x87_state.top;out[25]=out[26]=out[27]=0;
}
''')
    layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    elf=out/'probe.elf';cc=arm_proof['compiler'];compile([*cc,'-O2','-ffreestanding','-fno-stack-protector','-nostdlib','-I'+str(pc/'native'),str(entry),'@'+str(response(arm_proof,'arm.rsp')),'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-o',str(elf)],'arm-compiler.log')
    binary=out/'probe.bin';subprocess.run([str(Path(cc[0]).resolve().with_name('aarch64-none-elf-objcopy.exe')),'-O','binary',str(elf),str(binary)],check=True)
    arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x200000);arm.mem_write(0x100000,binary.read_bytes());arm.mem_map(0x800000,0x10000)
    for index,record in enumerate(records):
        arm.mem_write(0x800100,record);arm.mem_write(0x800200,b'\xa5'*32)
        arm.reg_write(ar.UC_ARM64_REG_X0,0x800100);arm.reg_write(ar.UC_ARM64_REG_X1,0x800200);arm.reg_write(ar.UC_ARM64_REG_SP,0x80f000);arm.reg_write(ar.UC_ARM64_REG_LR,0x2ff000)
        arm.emu_start(0x100000,0x2ff000,count=4000000)
        assert arm.reg_read(ar.UC_ARM64_REG_PC)==0x2ff000,index
        assert bytes(arm.mem_read(0x800200,32))==expected[index*28:index*28+28]+b'\xa5'*4,index
    verify(native);verify(arm_proof)
    paths=[Path(__file__).resolve(),Path(__file__).with_name('test_x87_integer_compare.py'),a.build.resolve(),a.arm_build.resolve(),pc/'tools/aot_x87_extended.py',pc/'tools/aot_frame_restore.py']
    result=dict(status='PASS',native_cases=len(records),arm64_translated_cases=len(records),source_sha256={str(p):sha(p) for p in paths},sdk_sha256=sdk_identity(a.sdk.resolve()),switch_hardware_tested=False,
        partial_reduction_checked=True,limits=['Masked canonical source operands; intermediate reduction chunks are implementation-dependent; final results/quotient bits and first-step C2 are compared'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('build','arm-build','sdk','python-deps','output'):parser.add_argument('--'+name,type=Path,required=True)
    test(parser.parse_args())

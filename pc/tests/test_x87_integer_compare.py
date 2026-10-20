"""Compare actual FICOM/FICOMP x86 opcodes with emitted ARM64 and core state."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(proof):
    assert proof['status']=='COMPILED'
    assert all(sha(Path(p))==h for p,h in proof['source_sha256'].items())
    assert all(sha(Path(e['object']))==e['sha256'] for e in proof['objects'])


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True);pc=Path(__file__).resolve().parents[1]
    native=json.loads(a.build.read_text());arm_proof=json.loads(a.arm_build.read_text())
    verify(native);verify(arm_proof)
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
    native_cases=[];arm_cases=[]
    forms=[(0xde,2,0),(0xde,2,1),(0xda,4,0),(0xda,4,1)]
    for index,(opcode,width,pop) in enumerate(forms):
        modrm=0x18 if pop else 0x10
        native_text=f'fldt %0; .byte {opcode},{modrm}; fnstsw 10(%%rdi)'
        if not pop:native_text+='; fstpt (%%rdi)'
        native_cases.append(f'case {index}: __asm__ volatile("'+native_text+'" : : "m"(*(const unsigned char (*)[10])input),"a"(input+10),"D"(out) : "memory","st");break;')
        raw=bytes.fromhex('db2d00002000')+bytes([opcode,modrm+5])+struct.pack('<I',0x200010)
        lifter=Lifter();code=[]
        for decoded in decoder.disasm(raw,0x400000+index*32):
            code+=lifter.lift_instruction(disassembler._decode_instruction(decoded))
        assert not lifter.unimplemented and any('fnv_x87_stack_compare_integer' in line for line in code)
        arm_cases.append(f'case {index}: '+'{\n'+'\n'.join(code)+'\nbreak;}')
    source=out/'oracle.c'
    source.write_text('''#include "x87_stack.h"
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
static void oracle(const unsigned char input[20],unsigned char out[16]) {
uint16_t old,cw=(uint16_t)(input[14]|input[15]<<8),sw;memset(out,0,16);
__asm__ volatile("fnstcw %0" : "=m"(old));
__asm__ volatile("fninit; fldcw %0" : : "m"(cw));
switch(input[16]) {
'''+ '\n'.join(native_cases)+'''
}
sw=(uint16_t)(out[10]|out[11]<<8);sw&=0x7f3f;out[10]=(unsigned char)sw;out[11]=(unsigned char)(sw>>8);
out[12]=(unsigned char)((sw>>11)&7);out[13]=(input[16]&1)?0:0x80;
__asm__ volatile("fnclex; fldcw %0" : : "m"(old));
}
int main(void) {
if(_setmode(_fileno(stdin),_O_BINARY)<0 || _setmode(_fileno(stdout),_O_BINARY)<0)return 1;
unsigned char input[20];unsigned count=0;
for(;;){size_t n=fread(input,1,20,stdin);if(!n)break;if(n!=20 || input[16]>3)return 2;
unsigned char expected[16],actual[16]={0};oracle(input,expected);
struct fnv_x87_stack s;fnv_x87_stack_init(&s);s.control=(uint16_t)(input[14]|input[15]<<8);
if(fnv_x87_stack_load(&s,input)!=FNV_X87_STACK_OK)return 3;
if(fnv_x87_stack_compare_integer(&s,input+10,input[16]<2?2:4,input[16]&1)!=FNV_X87_STACK_OK)return 4;
if(!(input[16]&1))fnv_x87_pack(actual,s.registers[s.top]);
actual[10]=(unsigned char)(s.status&0x3f);actual[11]=(unsigned char)((s.status&0x7f00)>>8);
actual[12]=s.top;actual[13]=s.occupied;
if(memcmp(actual,expected,16)){fprintf(stderr,"Mismatch case %u form %u status %04x/%04x\\n",count,input[16],s.status,expected[10]|expected[11]<<8);return 5;}
if(fwrite(expected,1,16,stdout)!=16)return 6;count++;}
fprintf(stderr,"PASS %u native FICOM/FICOMP cases\\n",count);return ferror(stdin)?7:0;
}
''')
    def response(proof,name):
        path=out/name;path.write_text('\n'.join('"'+e['object'].replace('\\','/')+'"' for e in proof['objects'])+'\n');return path
    exe=out/'oracle.exe'
    def run(command,log):
        r=subprocess.run(command,capture_output=True,text=True);(out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr[-3000:] or f'Compiler failed: {r.returncode}')
    run([*native['compiler'],'-O2','-I'+str(pc/'native'),str(source),'@'+str(response(native,'native.rsp')),'-Wl,--gc-sections','-o',str(exe)],'native-compiler.log')
    # Also execute the focused rejection/atomicity fixture against this exact core.
    fixture=pc/'tests/x87_integer_compare_host.c';fixture_exe=out/'fixture.exe'
    run([*native['compiler'],'-O2','-I'+str(pc/'native'),str(fixture),'@'+str(out/'native.rsp'),'-Wl,--gc-sections','-o',str(fixture_exe)],'fixture-compiler.log')
    subprocess.run([str(fixture_exe)],check=True)
    def dyadic(n,scale=0):
        if not n:return bytes(10)
        sign=0x8000 if n<0 else 0;n=abs(n);bits=n.bit_length();assert bits<=64
        return struct.pack('<QH',n<<(64-bits),sign|((bits-1-scale)+0x3fff))
    pairs=[]
    for n in (0,1,-1,32767,-32768,32768,-32769,16777217,-16777217,2147483647,-2147483648):
        for delta in (-1,0,1):pairs.append((dyadic(2*n+delta,1),n))
    pairs.extend((struct.pack('<QH',sig,exp),n) for sig,exp,n in [
        (0,0x8000,0),(1<<63,0x7fff,2147483647),(1<<63,0xffff,-2147483648),
        (0xc000000000001234,0x7fff,0),(0xc000000000001234,0xffff,-1),
        (0xffffffffffffffff,0x3fff,2),(0x8000000000000001,0x3fff,1)])
    rng=random.Random(0xf1c0)
    pairs.extend((dyadic(rng.randint(-(1<<60),1<<60),rng.randint(0,65)),rng.randint(-(1<<31),(1<<31)-1)) for _ in range(64))
    records=[]
    for form in range(4):
        for precision in (0,2,3):
            for rounding in range(4):
                for value,integer in pairs:
                    records.append(value+struct.pack('<IHBBBB',integer&0xffffffff,0x7f|(precision<<8)|(rounding<<10),form,0,0,0))
    data=b''.join(records);(out/'inputs.bin').write_bytes(data)
    r=subprocess.run([str(exe)],input=data,capture_output=True);(out/'oracle.log').write_bytes(r.stderr)
    if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace'))
    expected=r.stdout;assert len(expected)==len(records)*16;(out/'expected.bin').write_bytes(expected)
    entry=out/'entry.c';entry.write_text('''#include <stddef.h>
#include "x87_stack.h"
__attribute__((section(".text.entry"))) void probe(const unsigned char *request,unsigned char *out) {
struct fnv_x87_stack fnv_x87_state;fnv_x87_stack_init(&fnv_x87_state);
fnv_x87_state.control=(uint16_t)(request[14]|request[15]<<8);
unsigned char guest[32]={0};for(unsigned i=0;i<10;i++)guest[i]=request[i];for(unsigned i=0;i<4;i++)guest[16+i]=request[10+i];
ptrdiff_t g_xbox_mem_offset=(ptrdiff_t)guest-0x200000;
#define RECOMP_UNIMPL(text,va) do {out[0]=255;return;} while(0)
switch(request[16]) {
'''+ '\n'.join(arm_cases)+'''
}
for(unsigned i=0;i<16;i++)out[i]=0;
if(!(request[16]&1))fnv_x87_pack(out,fnv_x87_state.registers[fnv_x87_state.top]);
out[10]=(unsigned char)(fnv_x87_state.status&0x3f);out[11]=(unsigned char)((fnv_x87_state.status&0x7f00)>>8);
out[12]=fnv_x87_state.top;out[13]=fnv_x87_state.occupied;
}
''')
    layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    elf=out/'probe.elf';cc=arm_proof['compiler']
    run([*cc,'-O2','-ffreestanding','-fno-stack-protector','-nostdlib','-I'+str(pc/'native'),str(entry),'@'+str(response(arm_proof,'arm.rsp')),'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-o',str(elf)],'arm-compiler.log')
    binary=out/'probe.bin';subprocess.run([str(Path(cc[0]).resolve().with_name('aarch64-none-elf-objcopy.exe')),'-O','binary',str(elf),str(binary)],check=True)
    arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x200000);arm.mem_write(0x100000,binary.read_bytes());arm.mem_map(0x800000,0x10000)
    for index,record in enumerate(records):
        arm.mem_write(0x800100,record);arm.mem_write(0x800200,b'\xa5'*20)
        arm.reg_write(ar.UC_ARM64_REG_X0,0x800100);arm.reg_write(ar.UC_ARM64_REG_X1,0x800200)
        arm.reg_write(ar.UC_ARM64_REG_SP,0x80f000);arm.reg_write(ar.UC_ARM64_REG_LR,0x2ff000)
        arm.emu_start(0x100000,0x2ff000,count=20000)
        assert arm.reg_read(ar.UC_ARM64_REG_PC)==0x2ff000,index
        assert bytes(arm.mem_read(0x800200,20))==expected[index*16:index*16+16]+b'\xa5'*4,index
    verify(native);verify(arm_proof)
    paths=[Path(__file__).resolve(),fixture,a.build.resolve(),a.arm_build.resolve(),pc/'tools/aot_x87_extended.py',pc/'tools/aot_frame_restore.py']
    result=dict(status='PASS',native_cases=len(records),arm64_translated_cases=len(records),opcode_forms=4,
        focused_fixture_executed=True,source_sha256={str(p):sha(p) for p in paths},sdk_sha256=sdk_identity(a.sdk.resolve()),
        arm64_binary_sha256=sha(binary),switch_hardware_tested=False,
        limits=['Masked exceptions; extended denormals, signaling NaNs and stack-fault delivery remain outside the stack contract'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('build','arm-build','sdk','python-deps','output'):parser.add_argument('--'+name,type=Path,required=True)
    test(parser.parse_args())

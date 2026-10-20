"""Native-x87 oracle for ROM constants, raw ext80, FXAM, FFREE, FNINIT and WAIT.

Tests retain physical payloads even when a register is empty; C1 still exposes
their sign. Raw transport tests do not claim arithmetic on every encoding.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def verify(proof):
    assert proof['status']=='COMPILED'
    assert all(sha(Path(p))==h for p,h in proof['source_sha256'].items())
    assert all(sha(Path(e['object']))==e['sha256'] for e in proof['objects'])


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    native=json.loads(a.build.read_text());arm_proof=json.loads(a.arm_build.read_text());verify(native);verify(arm_proof)
    # Native helper and software-state helper produce identical 18-byte records.
    source=out/'oracle.c';source.write_text(r'''#include "x87_stack.h"
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
static void put(unsigned char *out,uint16_t x){out[0]=(unsigned char)x;out[1]=(unsigned char)(x>>8);}
static void hardware(const unsigned char in[16],unsigned char out[18]) {
uint16_t old,sw1,sw2,sw3,cw=(uint16_t)(in[10]|in[11]<<8),control;unsigned mode=in[12];
memset(out,0,18);__asm__ volatile("fnstcw %0":"=m"(old));__asm__ volatile("fninit;fldcw %0"::"m"(cw));
if(in[13])__asm__ volatile("fld1;fldz;.byte 0xde,0xf9,0xdd,0xd8":::"st","st(1)");
#define OP(text) __asm__ volatile(text:::"st")
switch(mode){case 0:OP("fldz");break;case 1:OP("fld1");break;case 2:OP("fldpi");break;
case 3:OP("fldl2e");break;case 4:OP("fldl2t");break;case 5:OP("fldlg2");break;case 6:OP("fldln2");break;
default: {
unsigned pushes=mode==9?8:1;for(unsigned j=0;j<pushes;j++)__asm__ volatile("fldt %0"::"m"(*(const unsigned char (*)[10])in):"st");
if(mode==8)OP(".byte 0xdd,0xc0");if(mode==9)OP("fninit");OP("fxam");break;}}
__asm__ volatile("fnstsw %0":"=m"(sw1));
if(mode==10)OP("fwait");if(mode==11)OP("fnop");
__asm__ volatile("fnstsw %0;fnstcw %1":"=m"(sw2),"=m"(control));
if(mode==8 || mode==9){memcpy(out,in,10);OP("fninit");}
else __asm__ volatile("fstpt %0":"=m"(*(unsigned char (*)[10])out)::"st");
__asm__ volatile("fnstsw %0":"=m"(sw3));
/* Constant-load C0/C2/C3 and store-ext80 C0/C2/C3 are undefined. */
if(mode<7){sw1&=(uint16_t)~0x4500u;sw2&=(uint16_t)~0x4500u;}
put(out+10,sw1);put(out+12,sw2);put(out+14,sw3&0x383f);put(out+16,control);
__asm__ volatile("fnclex;fldcw %0"::"m"(old));
#undef OP
}
static int software(const unsigned char in[16],unsigned char out[18]) {
struct fnv_x87_stack s;fnv_x87_stack_init(&s);s.control=(uint16_t)(in[10]|in[11]<<8);s.status=in[13]?4:0;
unsigned mode=in[12];memset(out,0,18);
if(mode<7){if(fnv_x87_stack_math_constant(&s,mode)!=FNV_X87_STACK_OK)return 1;}
else {
for(unsigned j=0;j<(mode==9?8u:1u);j++)if(fnv_x87_stack_load(&s,in)!=FNV_X87_STACK_OK)return 2;
if(mode==8 && fnv_x87_stack_free(&s,0)!=FNV_X87_STACK_OK)return 3;
if(mode==9)fnv_x87_stack_reset(&s);
if(fnv_x87_stack_examine(&s)!=FNV_X87_STACK_OK)return 4;
}
put(out+10,mode<7?s.status&~0x4500u:s.status);
if(mode>=10 && fnv_x87_stack_wait(&s)!=FNV_X87_STACK_OK)return 5;
put(out+12,mode<7?s.status&~0x4500u:s.status);put(out+16,s.control);
if(mode==8 || mode==9){memcpy(out,in,10);fnv_x87_stack_reset(&s);}
else if(fnv_x87_stack_store_pop(&s,out)!=FNV_X87_STACK_OK)return 6;
put(out+14,s.status&0x383f);return s.top || s.occupied;
}
static int failures(void) {
struct fnv_x87_stack s,before;fnv_x87_stack_init(&s);s.status=0x80;memcpy(&before,&s,sizeof(s));
if(fnv_x87_stack_wait(&s)!=FNV_X87_STACK_UNSUPPORTED || fnv_x87_stack_examine(&s)!=FNV_X87_STACK_UNSUPPORTED || memcmp(&s,&before,sizeof(s)))return 1;
s.status=1;s.control=0x37e;memcpy(&before,&s,sizeof(s));if(fnv_x87_stack_wait(&s)!=FNV_X87_STACK_UNSUPPORTED || memcmp(&s,&before,sizeof(s)))return 2;
s.control=0x37f;if(fnv_x87_stack_wait(&s)!=FNV_X87_STACK_OK)return 3;
s.status=0;s.control=0x37e;if(fnv_x87_stack_wait(&s)!=FNV_X87_STACK_OK)return 4;
fnv_x87_stack_init(&s);unsigned char unnormal[10]={1,0,0,0,0,0,0,0,0xff,0x3f},destination[8],saved[8];
memset(destination,0xa5,8);memcpy(saved,destination,8);
if(fnv_x87_stack_load(&s,unnormal)!=FNV_X87_STACK_OK)return 5;memcpy(&before,&s,sizeof(s));
if(fnv_x87_stack_store_binary(&s,destination,8,1)!=FNV_X87_STACK_UNSUPPORTED ||
fnv_x87_stack_duplicate(&s,0)!=FNV_X87_STACK_UNSUPPORTED || memcmp(&s,&before,sizeof(s)) || memcmp(destination,saved,8))return 6;
return 0;
}
int main(void){if(_setmode(_fileno(stdin),_O_BINARY)<0 || _setmode(_fileno(stdout),_O_BINARY)<0 || failures())return 1;
unsigned char in[16];unsigned count=0;for(;;){size_t n=fread(in,1,16,stdin);if(!n)break;if(n!=16 || in[12]>11)return 2;
unsigned char expected[18],actual[18];hardware(in,expected);if(software(in,actual))return 3;
if(memcmp(actual,expected,18)){fprintf(stderr,"Mismatch %u mode %u cw %04X\n",count,in[12],in[10]|in[11]<<8);for(unsigned i=0;i<18;i++)if(actual[i]!=expected[i])fprintf(stderr,"byte %u: %02X/%02X\n",i,actual[i],expected[i]);return 4;}
if(fwrite(expected,1,18,stdout)!=18)return 5;count++;}fprintf(stderr,"PASS %u special x87 cases and pending-exception guards\n",count);return ferror(stdin)?6:0;}
''')
    def response(proof,name):
        p=out/name;p.write_text('\n'.join('"'+e['object'].replace('\\','/')+'"' for e in proof['objects'])+'\n');return p
    exe=out/'oracle.exe'
    r=subprocess.run([*native['compiler'],'-O2','-I'+str(pc/'native'),str(source),'@'+str(response(native,'native.rsp')),'-Wl,--gc-sections','-o',str(exe)],capture_output=True,text=True)
    (out/'native-compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr[-3000:])
    values=[]
    for sig,exp in ((0,0),(1,0),(0x8000000000000000,0),(0xffffffffffffffff,0),
                    (0x8000000000000000,1),(0x8000000000000000,0x3fff),(1,0x3fff),
                    (0,0x7fff),(1,0x7fff),(0x8000000000000000,0x7fff),
                    (0xc000000000000001,0x7fff),(0x8000000000000001,0x7fff)):
        for sign in (0,0x8000):values.append(struct.pack('<QH',sig,exp|sign))
    rng=random.Random(0x87e5)
    for _ in range(32):values.append(struct.pack('<QH',rng.getrandbits(64),rng.randrange(65536)))
    records=[]
    for precision in (0,2,3):
        for rc in range(4):
            for ic in (0,1):
                cw=0x7f|(precision<<8)|(rc<<10)|(ic<<12)
                for mode in range(12):
                    for seed in (0,1):
                        for value in (values[:1] if mode<7 else values):records.append(value+struct.pack('<HBBH',cw,mode,seed,0))
    data=b''.join(records);(out/'inputs.bin').write_bytes(data)
    r=subprocess.run([str(exe)],input=data,capture_output=True);(out/'oracle.log').write_bytes(r.stderr);(out/'expected.bin').write_bytes(r.stdout)
    if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace'))
    assert len(r.stdout)==len(records)*18
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
    bodies=[];forms=0
    for mode in range(12):
        if mode<7:raw=bytes([0xd9,(0xee,0xe8,0xeb,0xea,0xe9,0xec,0xed)[mode]])
        else:
            raw=b'\xdb\x2d\x00\x00\x20\x00'*(8 if mode==9 else 1)
            if mode==8:raw+=b'\xdd\xc0'
            if mode==9:raw+=b'\xdb\xe3'
            raw+=b'\xd9\xe5'
        lifter=Lifter();lines=[]
        for decoded in decoder.disasm(raw,0x400000+mode*0x100):
            lines+=lifter.lift_instruction(disassembler._decode_instruction(decoded));forms+=1
        assert not lifter.unimplemented
        tail=[]
        if mode>=10:
            ins=disassembler._decode_instruction(next(decoder.disasm(b'\x9b' if mode==10 else b'\xd9\xd0',0x401000+mode*16)))
            tail=lifter.lift_instruction(ins);forms+=1;assert not lifter.unimplemented
        bodies.append(f'case {mode}: '+'{\n'+'\n'.join(lines)+
            '\nfirst=fnv_x87_state.status;\n'+'\n'.join(tail)+'\nbreak;}')
    entry=out/'entry.c';entry.write_text('''#include <stddef.h>
#include "x87_stack.h"
static void put(unsigned char *out,uint16_t v){out[0]=(unsigned char)v;out[1]=(unsigned char)(v>>8);}
__attribute__((section(".text.entry"))) void probe(const unsigned char *request,unsigned char *out) {
struct fnv_x87_stack fnv_x87_state;fnv_x87_stack_init(&fnv_x87_state);
fnv_x87_state.control=(uint16_t)(request[10]|request[11]<<8);fnv_x87_state.status=request[13]?4:0;
ptrdiff_t g_xbox_mem_offset=(ptrdiff_t)request-0x200000;uint16_t first=0;
#define RECOMP_UNIMPL(text,va) do {out[0]=255;return;} while(0)
switch(request[12]) {
'''+ '\n'.join(bodies)+'''
}
uint16_t second=fnv_x87_state.status;put(out+16,fnv_x87_state.control);
if(request[12]<7){first&=(uint16_t)~0x4500u;second&=(uint16_t)~0x4500u;}
put(out+10,first);put(out+12,second);
if(request[12]==8 || request[12]==9){for(unsigned i=0;i<10;i++)out[i]=request[i];fnv_x87_stack_reset(&fnv_x87_state);}
else if(fnv_x87_stack_store_pop(&fnv_x87_state,out)!=FNV_X87_STACK_OK){out[0]=254;return;}
put(out+14,fnv_x87_state.status&0x383f);if(fnv_x87_state.top || fnv_x87_state.occupied)out[0]=253;
}
''')
    layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    cc=arm_proof['compiler'];elf=out/'probe.elf'
    r=subprocess.run([*cc,'-O2','-ffreestanding','-fno-stack-protector','-nostdlib','-I'+str(pc/'native'),str(entry),'@'+str(response(arm_proof,'arm.rsp')),'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-o',str(elf)],capture_output=True,text=True)
    (out/'arm-compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr[-3000:])
    binary=out/'probe.bin';subprocess.run([str(Path(cc[0]).resolve().with_name('aarch64-none-elf-objcopy.exe')),'-O','binary',str(elf),str(binary)],check=True)
    arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x200000);arm.mem_write(0x100000,binary.read_bytes());arm.mem_map(0x800000,0x10000)
    expected=(out/'expected.bin').read_bytes()
    for index,record in enumerate(records):
        arm.mem_write(0x800100,record);arm.mem_write(0x800200,b'\xa5'*22)
        arm.reg_write(ar.UC_ARM64_REG_X0,0x800100);arm.reg_write(ar.UC_ARM64_REG_X1,0x800200)
        arm.reg_write(ar.UC_ARM64_REG_SP,0x80f000);arm.reg_write(ar.UC_ARM64_REG_LR,0x2ff000)
        arm.emu_start(0x100000,0x2ff000,count=10000)
        assert arm.reg_read(ar.UC_ARM64_REG_PC)==0x2ff000,index
        assert bytes(arm.mem_read(0x800200,22))==expected[index*18:index*18+18]+b'\xa5'*4,(index,record.hex())
    verify(native);verify(arm_proof)
    paths=[Path(__file__).resolve(),a.build.resolve(),a.arm_build.resolve(),pc/'tools/aot_x87_extended.py',pc/'tools/aot_frame_restore.py']
    result=dict(status='PASS',native_cases=len(records),arm64_translated_cases=len(records),translated_instruction_instances=forms,
                raw_patterns=len(values),constant_cases=7*24*2,pending_exception_guards=True,
                source_sha256={str(p):sha(p) for p in paths},sdk_sha256=sdk_identity(a.sdk.resolve()),
                oracle_source_sha256=sha(source),arm64_binary_sha256=sha(binary),runtime_integrated=False,switch_hardware_tested=False,
                limits=['Raw transport/classification does not imply arithmetic support for every encoding','No guest exception dispatch or hardware performance claim'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS',len(records),'native and translated ARM64 special cases')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','arm-build','sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    test(p.parse_args())

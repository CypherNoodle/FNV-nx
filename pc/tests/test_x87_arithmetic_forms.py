"""Check register destinations/reverse/pop and memory x87 arithmetic on x86/ARM64.

Native x87 executes the actual opcode bytes. All eight result registers and
defined exception flags are compared, including untouched stack slots.
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
    # Explicit opcode semantics, independent of emitted C. op: add/sub/mul/div.
    groups=[(0,0,0),(1,2,0),(4,1,0),(5,1,1),(6,3,0),(7,3,1)]
    forms=[]
    for kind in range(3):
        for index in range(8):
            for group,op,reverse in groups:
                if kind and group>=4:group^=1
                raw=bytes([(0xd8,0xdc,0xde)[kind],0xc0+group*8+index])
                forms.append(dict(raw=raw.hex(),op=op,reverse=reverse,destination=0 if kind==0 else index,
                                  source=index if kind==0 else 0,pop=int(kind==2),width=0,integer=0))
    for opcode,width,integer in ((0xd8,4,0),(0xdc,8,0),(0xde,2,1),(0xda,4,1)):
        for group,op,reverse in groups:
            forms.append(dict(raw=bytes([opcode,group*8]).hex(),op=op,reverse=reverse,
                              destination=0,source=0,pop=0,width=width,integer=integer))
    assert len(forms)==168
    metadata=[];native_cases=[];arm_cases=[];decoded_forms=[]
    for index,f in enumerate(forms):
        raw=bytes.fromhex(f['raw']);ins=disassembler._decode_instruction(next(decoder.disasm(raw,0x400000+index*16)))
        lifter=Lifter();code=lifter.lift_instruction(ins);assert not lifter.unimplemented,(f,ins)
        decoded_forms.append(dict(**f,mnemonic=ins.mnemonic,operands=ins.op_str))
        # Native memory forms address RAX; the same x86-32 encoding uses EAX.
        native_text='.byte '+','.join(hex(b) for b in raw)+'; '
        native_text+='; '.join('fstpt '+str(i*10)+'(%%rdi)' for i in range(8-f['pop']))+'; fnstsw 80(%%rdi)'
        loads='; '.join('fldt %'+str(i%2) for i in range(8))+'; '
        native_cases.append(f'case {index}: __asm__ volatile("'+loads+native_text+'" : : "m"(*(const unsigned char (*)[10])input),"m"(*(const unsigned char (*)[10])(input+10)),"a"(input+20),"D"(out) : "memory","st","st(1)","st(2)","st(3)","st(4)","st(5)","st(6)","st(7)");break;')
        metadata.append('{'+','.join(str(f[k]) for k in ('op','reverse','destination','source','pop','width','integer'))+'}')
        arm_cases.append(f'case {index}: '+'{\n'+'\n'.join(code)+'\nbreak;}')
    source=out/'oracle.c'
    source.write_text('''#include "x87_stack.h"
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
struct Form {unsigned op,reverse,destination,source,pop,width,integer;};
static const struct Form forms[]={'''+','.join(metadata)+'''};
static void oracle(const unsigned char input[32],unsigned form,uint16_t control,unsigned char out[82]) {
uint16_t old;memset(out,0,82);__asm__ volatile("fnstcw %0" : "=m"(old));
__asm__ volatile("fninit; fldcw %0" : : "m"(control));
switch(form) {
'''+ '\n'.join(native_cases)+'''
}
out[80]&=0x3f;out[81]=0;__asm__ volatile("fnclex; fldcw %0" : : "m"(old));
}
int main(void) {
if(_setmode(_fileno(stdin),_O_BINARY)<0 || _setmode(_fileno(stdout),_O_BINARY)<0)return 1;
unsigned char input[32];unsigned count=0;
for(;;) {
size_t n=fread(input,1,32,stdin);if(!n)break;if(n!=32)return 2;
unsigned form=input[30]|input[31]<<8;if(form>=168)return 3;
uint16_t control=(uint16_t)(input[28]|input[29]<<8);unsigned char expected[82],actual[82]={0};
oracle(input,form,control,expected);struct fnv_x87_stack state;fnv_x87_stack_init(&state);state.control=control;
for(unsigned i=0;i<8;i++)if(fnv_x87_stack_load(&state,input+(i%2)*10)!=FNV_X87_STACK_OK)return 4;
const struct Form *f=&forms[form];enum fnv_x87_stack_result r;
if(f->width)r=fnv_x87_stack_binary_memory(&state,(enum fnv_x87_operation)f->op,input+20,f->width,f->integer,f->reverse);
else r=fnv_x87_stack_binary_register(&state,(enum fnv_x87_operation)f->op,f->destination,f->source,f->pop,f->reverse);
if(r!=FNV_X87_STACK_OK)return 5;
for(unsigned i=0;i<8-f->pop;i++)if(fnv_x87_stack_store_pop(&state,actual+i*10)!=FNV_X87_STACK_OK)return 6;
actual[80]=(unsigned char)(state.status&0x3f);
if(memcmp(actual,expected,82) || state.top || state.occupied) {
fprintf(stderr,"Mismatch case %u form %u flags %02X/%02X\\n",count,form,actual[80],expected[80]);
for(unsigned i=0;i<80;i++)if(actual[i]!=expected[i]){fprintf(stderr,"byte %u %02X/%02X\\n",i,actual[i],expected[i]);break;}return 7;}
if(fwrite(expected,1,82,stdout)!=82)return 8;count++;
}
fprintf(stderr,"PASS %u native arithmetic form cases\\n",count);return ferror(stdin)?9:0;
}
''')
    def response(proof,name):
        p=out/name;p.write_text('\n'.join('"'+e['object'].replace('\\','/')+'"' for e in proof['objects'])+'\n');return p
    exe=out/'oracle.exe'
    r=subprocess.run([*native['compiler'],'-O2','-I'+str(pc/'native'),str(source),'@'+str(response(native,'native.rsp')),'-Wl,--gc-sections','-o',str(exe)],capture_output=True,text=True)
    (out/'native-compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr[-3000:])
    def value(sig,exp):return struct.pack('<QH',sig,exp)
    one=value(1<<63,0x3fff);three=value(0xc000000000000000,0x4000);zero=bytes(10)
    nan=value(0xc000000000001234,0x7fff);inf=value(1<<63,0x7fff)
    pairs=[(one,three,0x4004000040200000),(three,one,0xc0040000bfc00000),(zero,zero,0),
           (one,zero,0),(zero,inf,0),(inf,one,0),(one,nan,0x7ff000017f800001),
           (one,nan,0x8000000000000001),(nan,one,0x7ff800007fc00001),
           (nan,value(0xc000000000005678,0xffff),0x7ff000017f800001),
           (one,value((1<<63)+1,0x3fff),1),(value((1<<64)-1,0x7ffe),three,0x4000000040000000)]
    rng=random.Random(0x87a17)
    for _ in range(12):pairs.append((value((1<<63)|rng.getrandbits(63),rng.randint(1,0x7ffe)|(rng.randrange(2)<<15)),
        value((1<<63)|rng.getrandbits(63),rng.randint(1,0x7ffe)|(rng.randrange(2)<<15)),rng.getrandbits(64)))
    records=[]
    for precision in (0,2,3):
        for rc in range(4):
            cw=0x7f|(precision<<8)|(rc<<10)
            for index in range(len(forms)):
                for x,y,memory in pairs:records.append(x+y+struct.pack('<QHH',memory,cw,index))
    data=b''.join(records);(out/'inputs.bin').write_bytes(data)
    r=subprocess.run([str(exe)],input=data,capture_output=True);(out/'oracle.log').write_bytes(r.stderr);(out/'expected.bin').write_bytes(r.stdout)
    if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace'))
    assert len(r.stdout)==len(records)*82
    entry=out/'entry.c';entry.write_text('''#include <stddef.h>
#include "x87_stack.h"
__attribute__((section(".text.entry"))) void probe(const unsigned char *request,unsigned char *out) {
struct fnv_x87_stack fnv_x87_state;fnv_x87_stack_init(&fnv_x87_state);
fnv_x87_state.control=(uint16_t)(request[28]|request[29]<<8);
for(unsigned i=0;i<8;i++)if(fnv_x87_stack_load(&fnv_x87_state,request+(i%2)*10)!=FNV_X87_STACK_OK){out[0]=255;return;}
for(unsigned i=0;i<82;i++)out[i]=0;
ptrdiff_t g_xbox_mem_offset=(ptrdiff_t)(request+20)-0x200000;uint32_t eax=0x200000;
#define RECOMP_UNIMPL(text,va) do {out[0]=254;return;} while(0)
unsigned form=request[30]|request[31]<<8;
switch(form) {
'''+ '\n'.join(arm_cases)+'''
}
unsigned count=8;
'''+ '\n'.join(f'if(form=={i})count=7;' for i,f in enumerate(forms) if f['pop'])+'''
for(unsigned i=0;i<count;i++)if(fnv_x87_stack_store_pop(&fnv_x87_state,out+i*10)!=FNV_X87_STACK_OK){out[0]=253;return;}
out[80]=(unsigned char)(fnv_x87_state.status&0x3f);if(fnv_x87_state.top || fnv_x87_state.occupied)out[0]=252;
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
        arm.mem_write(0x800100,record);arm.mem_write(0x800200,b'\xa5'*86)
        arm.reg_write(ar.UC_ARM64_REG_X0,0x800100);arm.reg_write(ar.UC_ARM64_REG_X1,0x800200)
        arm.reg_write(ar.UC_ARM64_REG_SP,0x80f000);arm.reg_write(ar.UC_ARM64_REG_LR,0x2ff000)
        arm.emu_start(0x100000,0x2ff000,count=50000)
        assert arm.reg_read(ar.UC_ARM64_REG_PC)==0x2ff000,index
        assert bytes(arm.mem_read(0x800200,86))==expected[index*82:index*82+82]+b'\xa5'*4,(index,decoded_forms[struct.unpack_from('<H',record,30)[0]])
    verify(native);verify(arm_proof)
    paths=[Path(__file__).resolve(),a.build.resolve(),a.arm_build.resolve(),pc/'tools/aot_x87_extended.py',pc/'tools/aot_frame_restore.py']
    result=dict(status='PASS',native_cases=len(records),arm64_translated_cases=len(records),instruction_forms=len(forms),
                forms=decoded_forms,source_sha256={str(p):sha(p) for p in paths},sdk_sha256=sdk_identity(a.sdk.resolve()),
                oracle_source_sha256=sha(source),arm64_binary_sha256=sha(binary),runtime_integrated=False,switch_hardware_tested=False,
                limits=['Masked canonical register operands','No C1, unmasked exceptions or extended-denormal register operands'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS',len(records),'native and translated ARM64 cases;',len(forms),'instruction forms')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','arm-build','sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    test(p.parse_args())

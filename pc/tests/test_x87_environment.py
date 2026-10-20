"""Synthetic native-x87 vs ARM64 checks for protected-mode 28/108-byte images.

The C API is exercised directly. AOT environment instructions remain disabled
until guest instruction/data pointer tracking has been integrated and audited.
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
def verify(d):
    assert d['status']=='COMPILED'
    assert all(sha(Path(p))==h for p,h in d['source_sha256'].items())
    assert all(sha(Path(o['object']))==o['sha256'] for o in d['objects'])


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    native=json.loads(a.build.read_text());arm=json.loads(a.arm_build.read_text());verify(native);verify(arm)
    source=out/'oracle.c'
    source.write_text(r'''#include "x87_stack.h"
#include <stddef.h>
void *memcpy(void *to,const void *from,size_t n){unsigned char *d=to;const unsigned char *s=from;for(size_t i=0;i<n;i++)d[i]=s[i];return to;}
void *memset(void *to,int value,size_t n){unsigned char *d=to;for(size_t i=0;i<n;i++)d[i]=(unsigned char)value;return to;}
static int equal(const void *a,const void *b,size_t n){const unsigned char *x=a,*y=b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return 0;return 1;}
__attribute__((section(".text.entry"))) int software(const unsigned char in[137],unsigned char out[216]) {
struct fnv_x87_stack s;struct fnv_x87_environment e={0};fnv_x87_stack_init(&s);memset(out,0,216);
if(fnv_x87_stack_restore32(&s,&e,in,108,1)!=FNV_X87_STACK_OK)return 1;
unsigned mode=in[136];if(mode==2 && fnv_x87_stack_restore32(&s,&e,in+108,28,0)!=FNV_X87_STACK_OK)return 2;
if(fnv_x87_stack_save32(&s,&e,out,108,mode==1 || mode==3?0:1)!=FNV_X87_STACK_OK)return 3;
if(mode==3 && fnv_x87_stack_restore32(&s,&e,out,28,0)!=FNV_X87_STACK_OK)return 4;
if(fnv_x87_stack_save32(&s,&e,out+108,108,1)!=FNV_X87_STACK_OK)return 5;
return s.top || s.occupied || s.control!=0x37f || s.status || e.instruction_pointer || e.data_pointer || e.opcode || e.code_selector || e.data_selector;
}
#ifdef NATIVE_ORACLE
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
static void hardware(const unsigned char in[137],unsigned char out[216]) {
memset(out,0,216);
/* Keep the instruction sequence in one volatile asm block: the compiler may
 * not insert code between restoring guest metadata and observing it. This is
 * still a host OS oracle; any divergent hardware output remains a hard failure. */
#define INPUT "m"(*(const unsigned char(*)[108])in)
#define ENV "m"(*(const unsigned char(*)[28])(in+108))
#define OUT "=m"(*(unsigned char(*)[108])out),"=m"(*(unsigned char(*)[108])(out+108))
switch(in[136]){
case 0:__asm__ volatile("fninit;frstor %2;fnsave %0;fnsave %1":OUT:INPUT:"st","memory");break;
case 1:__asm__ volatile("fninit;frstor %2;fnstenv %0;fnsave %1":OUT:INPUT:"st","memory");break;
case 2:__asm__ volatile("fninit;frstor %2;fldenv %3;fnsave %0;fnsave %1":OUT:INPUT,ENV:"st","memory");break;
case 3:__asm__ volatile("fninit;frstor %2;fnstenv %0;fldenv %0;fnsave %1":OUT:INPUT:"st","memory");break;
}
#undef OUT
#undef ENV
#undef INPUT
}
static int pointer_checks(void){
unsigned char image[28];uintptr_t expected;
#define FIP ((uint32_t)image[12]|(uint32_t)image[13]<<8|(uint32_t)image[14]<<16|(uint32_t)image[15]<<24)
__asm__ volatile("fninit;leaq 1f(%%rip),%1;1: fld1;fnstenv %0;fninit":"=m"(*(unsigned char(*)[28])image),"=&r"(expected): :"st","memory");
if(FIP!=(uint32_t)expected)return 1;
__asm__ volatile("fninit;leaq 1f(%%rip),%1;1: .byte 0x66,0xd9,0xe8;fnstenv %0;fninit":"=m"(*(unsigned char(*)[28])image),"=&r"(expected): :"st","memory");
if(FIP!=(uint32_t)expected)return 2;
__asm__ volatile("fninit;fld1;leaq 1f(%%rip),%1;1: fnop;fnstenv %0;fninit":"=m"(*(unsigned char(*)[28])image),"=&r"(expected): :"st","memory");
if(FIP!=(uint32_t)expected)return 3;
__asm__ volatile("fninit;leaq 1f(%%rip),%1;1: fld1;fwait;fnclex;fnstenv %0;fninit":"=m"(*(unsigned char(*)[28])image),"=&r"(expected): :"st","memory");
if(FIP!=(uint32_t)expected)return 4;
__asm__ volatile("fninit;fld1;fninit;fnstenv %0":"=m"(*(unsigned char(*)[28])image): :"st","memory");
if(FIP)return 5;
#undef FIP
return 0;
}
static int negatives(void){
struct fnv_x87_stack s,before;struct fnv_x87_environment e={0},old;unsigned char image[108],saved[108];
fnv_x87_stack_init(&s);memset(image,0xa5,108);memcpy(saved,image,108);before=s;old=e;
if(fnv_x87_stack_save32(&s,&e,image,107,1)!=FNV_X87_STACK_INVALID || !equal(image,saved,108) || !equal(&s,&before,sizeof(s)))return 1;
e.opcode=0x800;old=e;if(fnv_x87_stack_save32(&s,&e,image,108,1)!=FNV_X87_STACK_UNSUPPORTED || !equal(image,saved,108) || !equal(&s,&before,sizeof(s)))return 2;
e.opcode=0;if(fnv_x87_stack_save32(&s,&e,image,108,1)!=FNV_X87_STACK_OK)return 3;
for(unsigned mode=0;mode<4;mode++){unsigned char changed[108];memcpy(changed,image,108);
if(mode==0)changed[0]&=0xfe;if(mode==1)changed[1]=1;if(mode==2)changed[4]|=0x80;if(mode==3)changed[5]|=0x80;
before=s;old=e;if(fnv_x87_stack_restore32(&s,&e,changed,108,1)!=FNV_X87_STACK_UNSUPPORTED || !equal(&s,&before,sizeof(s)) || !equal(&e,&old,sizeof(e)))return 4;}
for(unsigned mask=0;mask<64;mask++){
uint16_t cw=(uint16_t)(0x340u|mask),after;unsigned char hw[28];
__asm__ volatile("fninit;fldcw %2;fnstenv %0;fnstcw %1;fninit":"=m"(*(unsigned char(*)[28])hw),"=m"(after):"m"(cw):"st");
fnv_x87_stack_init(&s);s.control=cw;e=(struct fnv_x87_environment){0};before=s;
if(fnv_x87_stack_save32(&s,&e,image,108,0)!=FNV_X87_STACK_OK ||
image[0]!=hw[0] || image[1]!=hw[1] || s.control!=after || s.occupied || s.top || s.status)return 6;
}
fnv_x87_stack_init(&s);e=(struct fnv_x87_environment){0};
s.status=0x80;before=s;old=e;if(fnv_x87_stack_restore32(&s,&e,image,108,1)!=FNV_X87_STACK_UNSUPPORTED || !equal(&s,&before,sizeof(s)) || !equal(&e,&old,sizeof(e)))return 5;
return 0;
}
int main(void){if(_setmode(_fileno(stdin),_O_BINARY)<0 || _setmode(_fileno(stdout),_O_BINARY)<0 || negatives() || pointer_checks())return 1;
unsigned char in[137],expected[216],actual[216];unsigned count=0;for(;;){size_t n=fread(in,1,137,stdin);if(!n)break;if(n!=137 || in[136]>3)return 2;
hardware(in,expected);if(software(in,actual))return 3;
if(!equal(expected,actual,216)){unsigned char repeated[216];hardware(in,repeated);fprintf(stderr,"Native repeat matches first=%d software=%d (no retry acceptance)\n",equal(repeated,expected,216),equal(repeated,actual,216));fprintf(stderr,"Mismatch case %u mode %u\n",count,in[136]);for(unsigned i=0;i<216;i++)if(actual[i]!=expected[i])fprintf(stderr,"byte %u: %02x/%02x\n",i,actual[i],expected[i]);return 4;}
if(fwrite(expected,1,216,stdout)!=216)return 5;count++;}fprintf(stderr,"PASS %u environment cases\n",count);return ferror(stdin)?6:0;}
#endif
''')
    def rsp(proof,name):
        p=out/name;p.write_text('\n'.join('"'+o['object'].replace('\\','/')+'"' for o in proof['objects'])+'\n');return p
    exe=out/'oracle.exe'
    command=[*native['compiler'],'-O2','-fno-builtin','-DNATIVE_ORACLE','-I'+str(pc/'native'),str(source),'@'+str(rsp(native,'native.rsp')),'-Wl,--gc-sections','-o',str(exe)]
    r=subprocess.run(command,capture_output=True,text=True);(out/'native-compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr[-3000:])
    rng=random.Random(0x8732108);records=[]
    values=[(0,0),(0,0x8000),(0x8000000000000000,0x3fff),(0x8000000000000000,0x7fff),
            (0xc000000000000001,0x7fff),(1,0),(0x8000000000000000,0),(1,0x3fff),(0x8000000000000001,0x7fff)]
    def image(top,iteration,full):
        cw=0x7f|((0,2,3)[iteration%3]<<8)|((iteration%4)<<10)|((iteration%2)<<12)
        status=(top<<11)|(rng.randrange(32)<<8&0x4700)|rng.randrange(128)
        # All exceptions masked: loaded ES/B are zero. Tags intentionally need
        # reclassification; only empty/nonempty survives FRSTOR/FLDENV.
        tags=rng.randrange(65536);data=bytearray(108 if full else 28)
        struct.pack_into('<H2xH2xH2xIHHIH',data,0,cw,status,tags,rng.getrandbits(32),rng.randrange(65536),rng.randrange(2048),rng.getrandbits(32),rng.randrange(65536))
        if full:
            for i in range(8):
                sig,exp=values[(iteration+i)%len(values)] if iteration<128 else (rng.getrandbits(64),rng.randrange(65536))
                data[28+i*10:38+i*10]=struct.pack('<QH',sig,exp)
        return bytes(data)
    for top in range(8):
        for index in range(256):
            first=image(top,index,True);second=image((top+index)%8,index+1,False)
            for mode in range(4):records.append(first+second+bytes([mode]))
    data=b''.join(records);(out/'inputs.bin').write_bytes(data)
    r=subprocess.run([str(exe)],input=data,capture_output=True);(out/'oracle.log').write_bytes(r.stderr)
    if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace'))
    expected=r.stdout;assert len(expected)==len(records)*216;(out/'expected.bin').write_bytes(expected)
    sys.path.insert(0,str(a.python_deps.resolve()))
    from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
    import unicorn.arm64_const as ar
    layout=out/'layout.ld';layout.write_text('ENTRY(software)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    elf=out/'probe.elf';cc=arm['compiler']
    r=subprocess.run([*cc,'-O2','-fno-builtin','-ffreestanding','-fno-stack-protector','-nostdlib','-I'+str(pc/'native'),str(source),'@'+str(rsp(arm,'arm.rsp')),'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-o',str(elf)],capture_output=True,text=True)
    (out/'arm-compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr[-3000:])
    binary=out/'probe.bin';subprocess.run([str(Path(cc[0]).resolve().with_name('aarch64-none-elf-objcopy.exe')),'-O','binary',str(elf),str(binary)],check=True)
    uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM);uc.mem_map(0x100000,0x200000);uc.mem_write(0x100000,binary.read_bytes());uc.mem_map(0x800000,0x10000)
    for index,record in enumerate(records):
        uc.mem_write(0x800100,record);uc.mem_write(0x800300,b'\xa5'*224)
        uc.reg_write(ar.UC_ARM64_REG_X0,0x800100);uc.reg_write(ar.UC_ARM64_REG_X1,0x800300)
        uc.reg_write(ar.UC_ARM64_REG_SP,0x80f000);uc.reg_write(ar.UC_ARM64_REG_LR,0x2ff000)
        uc.emu_start(0x100000,0x2ff000,count=20000)
        assert uc.reg_read(ar.UC_ARM64_REG_PC)==0x2ff000 and uc.reg_read(ar.UC_ARM64_REG_X0)==0,index
        assert bytes(uc.mem_read(0x800300,224))==expected[index*216:(index+1)*216]+b'\xa5'*8,index
    verify(native);verify(arm)
    paths=[Path(__file__).resolve(),a.build.resolve(),a.arm_build.resolve()]
    result=dict(status='PASS',native_cases=len(records),arm64_api_cases=len(records),modes=4,all_top_positions=True,
        negative_rollback_checks=True,exception_mask_transition_cases=64,native_instruction_pointer_checks=5,source_sha256={str(p):sha(p) for p in paths},oracle_source_sha256=sha(source),
        arm64_binary_sha256=sha(binary),aot_environment_instructions_enabled=False,runtime_integrated=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS',len(records),'native and ARM64 environment API cases')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','arm-build','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    test(p.parse_args())

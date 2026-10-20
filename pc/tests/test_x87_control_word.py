"""Compare FLDCW reserved-bit normalization with every native x86 word.

The optional candidate changes an isolated copy of our stack source only.
Unmasked exceptions, reserved precision and pending ES remain unsupported.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True);pc=Path(__file__).resolve().parents[1]
    sys.path[:0]=[str(a.python_deps.resolve()),str(pc/'tools')]
    from build_cpu_runtime import SWITCH_FLAGS
    from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
    import unicorn.arm64_const as ar
    oracle=pc/'tests/x87_control_word_oracle.c';original=pc/'native/x87_stack.c'
    paths=[Path(__file__).resolve(),oracle,original,pc/'native/x87_stack.h',pc/'native/x87_extended.h',
           pc/'tools/build_cpu_runtime.py',a.arm_cc.resolve(),Path(a.native_cc[0]).resolve()]
    frozen={str(p):sha(p) for p in paths}
    def run(args,name,**kw):
        r=subprocess.run([str(x) for x in args],capture_output=True,**kw)
        (out/name).write_bytes(r.stderr)
        if r.returncode:raise RuntimeError(name+': '+r.stderr.decode(errors='replace')[-3000:])
        return r.stdout
    exe=out/'oracle.exe'
    run([*a.native_cc,'-target','x86-windows-gnu','-O2',oracle,'-o',exe],'native-build.log')
    expected=run([exe],'oracle.log');(out/'native.bin').write_bytes(expected)
    if len(expected)!=131072:raise AssertionError('Incomplete native control-word readback')
    for n,(actual,) in enumerate(struct.iter_unpack('<H',expected)):
        if actual!=((n&0x1f3f)|0x40):raise AssertionError(('Native normalization changed',n,actual))
    source=original
    if a.candidate:
        text=source.read_text();old='uint16_t value=(uint16_t)(bytes[0]|(uint16_t)bytes[1]<<8);'
        new='uint16_t value=(uint16_t)(((bytes[0]|(uint16_t)bytes[1]<<8)&0x1f3fu)|0x40u);'
        if text.count(old)!=1:raise ValueError('Candidate source site changed')
        source=out/'x87_stack_candidate.c';source.write_text(text.replace(old,new),newline='\n')
        frozen[str(source)]=sha(source)
    probe=out/'probe.c';probe.write_text('''#include "x87_stack.h"
#include <stddef.h>
void *memset(void *p,int x,size_t n){unsigned char *d=p;for(size_t i=0;i<n;i++)d[i]=(unsigned char)x;return p;}
__attribute__((section(".text.entry"))) void probe(unsigned word,unsigned mode,unsigned char *out){
struct fnv_x87_stack s;unsigned char *p=(unsigned char*)&s;
for(unsigned i=0;i<sizeof s;i++)p[i]=(unsigned char)(i*17+3);
s.top=3;s.occupied=0x88;s.control=0x37f;s.status=0x3a21;
if(mode==1)s.status|=0x80;if(mode==2)s.top=8;
unsigned char before[sizeof s];for(unsigned i=0;i<sizeof s;i++)before[i]=p[i];
unsigned char bytes[2]={(unsigned char)word,(unsigned char)(word>>8)};
int r=fnv_x87_stack_control(mode==3?0:&s,mode==4?0:bytes);
unsigned same=1,other=1;for(unsigned i=0;i<sizeof s;i++){
if(p[i]!=before[i])same=0;
if((i<offsetof(struct fnv_x87_stack,control)||i>=offsetof(struct fnv_x87_stack,control)+2)&&p[i]!=before[i])other=0;}
out[0]=(unsigned char)r;out[1]=(unsigned char)s.control;out[2]=(unsigned char)(s.control>>8);out[3]=(unsigned char)same;out[4]=(unsigned char)other;
}
''',newline='\n')
    obj=out/'stack.o';includes=['-I'+str(pc/'native')]
    run([a.arm_cc,*SWITCH_FLAGS,*includes,'-c',source,'-o',obj],'arm-build.log')
    layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    elf=out/'probe.elf';binary=out/'probe.bin'
    run([a.arm_cc,*SWITCH_FLAGS,'-fno-builtin','-ffreestanding','-nostdlib',*includes,probe,obj,'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-o',elf],'link.log')
    run([a.arm_cc.with_name('aarch64-none-elf-objcopy.exe'),'-O','binary',elf,binary],'objcopy.log')
    u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(0x100000,0x100000);u.mem_write(0x100000,binary.read_bytes());u.mem_map(0x800000,0x10000)
    def execute(n,mode):
        u.mem_write(0x800100,b'\xa5'*12)
        for reg,v in ((ar.UC_ARM64_REG_X0,n),(ar.UC_ARM64_REG_X1,mode),(ar.UC_ARM64_REG_X2,0x800100),(ar.UC_ARM64_REG_SP,0x80f000),(ar.UC_ARM64_REG_LR,0x1ff000)):u.reg_write(reg,v)
        u.emu_start(0x100000,0x1ff000,count=100000)
        if u.reg_read(ar.UC_ARM64_REG_PC)!=0x1ff000:raise AssertionError('ARM64 probe did not return')
        result=bytes(u.mem_read(0x800100,12))
        if result[5:]!=b'\xa5'*7:raise AssertionError('Output canary changed')
        return result[:5]
    accepted=rejected=0
    for n in range(65536):
        r=execute(n,0);cw=struct.unpack_from('<H',expected,n*2)[0]
        if n&0x3f==0x3f and (n>>8)&3!=1:
            if r[0] or int.from_bytes(r[1:3],'little')!=cw or not r[4]:raise AssertionError(('Control normalization mismatch',n,r.hex()))
            accepted+=1
        else:
            if r[0]!=4 or not r[3]:raise AssertionError(('Unsupported control changed state',n,r.hex()))
            rejected+=1
    negatives=0
    for n in (0x23f,0x27f,0x37f,0xffff,0):
        for mode in range(1,5):
            r=execute(n,mode)
            if r[0]!=(4 if mode==1 else 1) or not r[3]:raise AssertionError('Precondition failure changed state')
            negatives+=1
    if any(sha(Path(p))!=h for p,h in frozen.items()):raise ValueError('Inputs changed during verification')
    result=dict(status='CANDIDATE_PASS' if a.candidate else 'PASS',native_control_words=65536,arm64_control_words=65536,
                accepted_masked_controls=accepted,rejected_unsupported_controls=rejected,atomic_negative_cases=negatives,
                native_sha256=sha(out/'native.bin'),production_sources_modified=False,hardware_tested=False,
                compiler_flags=SWITCH_FLAGS,arm_stack_object_sha256=sha(obj),source_sha256=frozen)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('python-deps','arm-cc','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--native-cc',nargs='+',required=True);p.add_argument('--candidate',action='store_true')
    test(p.parse_args())

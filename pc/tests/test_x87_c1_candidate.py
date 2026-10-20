"""Isolated rounded-up C1 candidate vs native x87 and real ARM64 objects.

Does not change the production core or any in-flight generation/build input.
"""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess
import sys


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    sys.path[:0]=[str(pc/'tools'),str(a.python_deps.resolve())]
    from run_aot_analysis import sha
    from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
    import unicorn.arm64_const as ar
    host=json.loads(a.host_core.read_text());arm=json.loads(a.arm_core.read_text())
    if host['status']!='COMPILED' or host['switch_abi'] or arm['status']!='COMPILED' or not arm['switch_abi']:
        raise ValueError('Require native-host and actual Switch arithmetic cores')
    frozen={}
    for proof in (host,arm):
        frozen.update({Path(p):h for p,h in proof['source_sha256'].items()})
        frozen.update({Path(o['object']):o['sha256'] for o in proof['objects']})
    candidate=pc/'tests'/('x87_c1_integer_candidate.c' if a.integer else 'x87_c1_candidate.c')
    for p in [Path(__file__).resolve(),candidate,pc/'tests/x87_c1_candidate.h',pc/'tests/x87_c1_oracle.c',a.host_core.resolve(),a.arm_core.resolve()]:frozen[p]=sha(p)
    if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Stale core/input')
    def run(args,log):
        r=subprocess.run([str(v) for v in args],capture_output=True,text=True)
        (out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr[-4000:])
        return r.stdout
    includes=['-I'+str(pc/'native'),'-I'+str(pc/'tests')]
    def candidate_object(proof,label):
        obj=out/(label+'-candidate.o')
        run([*proof['compiler'],'-std=c11','-O1','-Wall','-Wextra','-Werror',*includes,
             '-Dfnv_x87_calculate=fnv_counted_original','-c',candidate,'-o',obj],label+'-candidate.log')
        return obj
    def response(name,objects):
        p=out/name;p.write_text('\n'.join('"'+str(o).replace('\\','/')+'"' for o in objects)+'\n');return '@'+str(p)
    host_obj=candidate_object(host,'host');host_rsp=response('host-core.rsp',[o['object'] for o in host['objects']])
    exe=out/'oracle.exe'
    run([*host['compiler'],'-O1',*includes,pc/'tests/x87_c1_oracle.c',host_obj,host_rsp,'-o',exe],'host-oracle-compile.log')
    def ext(sig,exp):return struct.pack('<QH',sig,exp)
    one=ext(1<<63,0x3fff);zero=ext(0,0)
    pairs=[(one,ext(1<<63,0x3fe7)),(one,ext(1<<63,0x3fbf)),(one,one),(zero,one),
           (ext(1<<63,0xbfff),one),(ext((1<<64)-1,0x7ffe),ext(1<<63,0x4000)),
           (ext(1<<63,1),ext(1<<63,0x3ffe)),(one,zero)]
    for delta in (0,1,23,24,52,53,63,64,65,127,128):
        for sign in (0,0x8000):
            pairs.append((ext((1<<64)-1,0x3fff),ext((1<<64)-1,sign|(0x3fff-delta))))
    for sig in (1<<63,(1<<63)+1,(1<<64)-1):
        pairs.extend([(ext(sig,1),ext(1<<63,0x3ffe)),(ext(sig,0x7ffe),ext(1<<63,0x4000)),
                      (ext(sig,0x3fff),ext(sig,0xbfff))])
    rng=random.Random(0xc187)
    for unused in range(160):
        def value():return ext(rng.randrange(1<<63,1<<64),(0x8000 if rng.randrange(2) else 0)|(0x3fff+rng.randrange(-1000,1001)))
        pairs.append((value(),value()))
    records=[bytes([op,0])+struct.pack('<H',0x7f|(pc_mode<<8)|(rc<<10))+x+y+b'\0'*8
             for x,y in pairs for op in range(5) for pc_mode in (0,2,3) for rc in range(4)]
    native=subprocess.run([str(exe)],input=b''.join(records),capture_output=True)
    (out/'native.log').write_bytes(native.stderr)
    if native.returncode or len(native.stdout)!=40*len(records):raise RuntimeError('Native C1 oracle failed')
    (out/'requests.bin').write_bytes(b''.join(records));(out/'native-results.bin').write_bytes(native.stdout)
    native_fail=[];base_c1_differences=0;extra_evaluations=0
    def check(index,actual,expected):
        f=struct.unpack_from('<H',expected,10)[0];mask=0x3f if f&1 else 0x23f
        return actual[:10]==expected[:10] and not ((struct.unpack_from('<H',actual,10)[0]^f)&mask)
    for i in range(len(records)):
        row=native.stdout[i*40:(i+1)*40];f=struct.unpack_from('<H',row,10)[0];base=struct.unpack_from('<H',row,34)[0]
        if not check(i,row[:12],row[12:24]):native_fail.append(i)
        if row[:10]!=row[24:34] or (f&~0x200)!=base:raise AssertionError('Candidate changed base payload/exception flags')
        if not (struct.unpack_from('<H',row,22)[0]&1):base_c1_differences+=bool((base^struct.unpack_from('<H',row,22)[0])&0x200)
        extra_evaluations+=struct.unpack_from('<I',row,36)[0]-1
    if native_fail:raise AssertionError(('Candidate/native C1 mismatches',native_fail[:12]))
    source=out/'arm.c';source.write_text('''#include "x87_c1_candidate.h"
#include <stddef.h>
void *memcpy(void *d,const void *s,size_t n){unsigned char *a=d;const unsigned char *b=s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;}
void *memset(void *d,int c,size_t n){unsigned char *a=d;for(size_t i=0;i<n;i++)a[i]=(unsigned char)c;return d;}
static unsigned evaluations;
int fnv_counted_original(enum fnv_x87_operation op,uint16_t cw,struct fnv_x87_extended a,struct fnv_x87_extended b,struct fnv_x87_extended *r,uint16_t *f){evaluations++;return fnv_x87_calculate(op,cw,a,b,r,f);}
__attribute__((section(".text.entry"))) int probe(const unsigned char *in,unsigned char *out){
struct fnv_x87_extended a=fnv_x87_unpack(in+4),b=fnv_x87_unpack(in+14),r;uint16_t f;
evaluations=0;if(!fnv_x87_c1_candidate((enum fnv_x87_operation)in[0],(uint16_t)(in[2]|in[3]<<8),a,b,&r,&f))return 1;
fnv_x87_pack(out,r);out[10]=(unsigned char)f;out[11]=(unsigned char)(f>>8);for(unsigned i=0;i<4;i++)out[12+i]=(unsigned char)(evaluations>>(i*8));return 0;}
''')
    layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    obj=candidate_object(arm,'arm');rsp=response('arm-core.rsp',[o['object'] for o in arm['objects']]);elf=out/'test.elf';binary=out/'test.bin'
    run([*arm['compiler'],'-O1','-fno-builtin','-ffreestanding','-fno-stack-protector','-nostdlib',*includes,source,obj,rsp,'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-o',elf],'arm-link.log')
    run([Path(arm['compiler'][0]).with_name('aarch64-none-elf-objcopy.exe'),'-O','binary',elf,binary],'objcopy.log')
    u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(0x100000,0x200000);u.mem_write(0x100000,binary.read_bytes());u.mem_map(0x800000,0x10000)
    failures=[]
    for i,record in enumerate(records):
        u.mem_write(0x800100,record);u.mem_write(0x800400,b'\xa5'*24)
        for reg,v in ((ar.UC_ARM64_REG_X0,0x800100),(ar.UC_ARM64_REG_X1,0x800400),(ar.UC_ARM64_REG_SP,0x80f000),(ar.UC_ARM64_REG_LR,0x2ff000),(ar.UC_ARM64_REG_FPCR,0x400000),(ar.UC_ARM64_REG_FPSR,0x10)):u.reg_write(reg,v)
        u.emu_start(0x100000,0x2ff000,count=100000)
        actual=bytes(u.mem_read(0x800400,24));row=native.stdout[i*40:(i+1)*40]
        if (u.reg_read(ar.UC_ARM64_REG_PC)!=0x2ff000 or u.reg_read(ar.UC_ARM64_REG_X0)!=0 or
            actual[:12]!=row[:12] or actual[12:16]!=row[36:40] or actual[16:]!=b'\xa5'*8 or
            u.reg_read(ar.UC_ARM64_REG_FPCR)!=0x400000 or u.reg_read(ar.UC_ARM64_REG_FPSR)!=0x10):failures.append(i)
    if failures:raise AssertionError(('ARM C1 candidate mismatch',failures[:12]))
    if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Source/core inputs changed')
    if a.integer and extra_evaluations:raise AssertionError('Integer candidate repeated the arithmetic operation')
    proof=dict(status='CANDIDATE_PASS',strategy='integer-comparison' if a.integer else 'repeat-truncated',native_cases=len(records),arm_cases=len(records),
               old_core_c1_differences=base_c1_differences,candidate_c1_differences=0,
               additional_core_evaluations=extra_evaluations,exception_payload_regressions=0,
               production_core_modified=False,stack_status_update_integrated=False,hardware_tested=False,
               requests_sha256=sha(out/'requests.bin'),native_results_sha256=sha(out/'native-results.bin'),
               source_sha256={str(p):h for p,h in frozen.items()})
    (out/'validation.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({k:v for k,v in proof.items() if k!='source_sha256'},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('host-core','arm-core','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--integer',action='store_true',help='Test the exact integer-comparison candidate')
    test(p.parse_args())

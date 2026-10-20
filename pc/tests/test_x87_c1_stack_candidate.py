"""Private candidate-stack copy checks C1 replacement and unchanged other state.

Uses original project source only; never patches production sources or objects.
"""
import argparse
import json
from pathlib import Path
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
    proof=json.loads(a.arithmetic_proof.read_text());base=a.arithmetic_proof.resolve().parent
    if proof['status']!='CANDIDATE_PASS' or proof['strategy']!='integer-comparison':raise ValueError('Require verified integer candidate')
    core=json.loads(a.core.read_text());cc=core['compiler']
    frozen={Path(p):h for p,h in proof['source_sha256'].items()}
    frozen.update({Path(p):h for p,h in core['source_sha256'].items()})
    frozen.update({Path(o['object']):o['sha256'] for o in core['objects']})
    data=(base/'requests.bin').read_bytes();native=(base/'native-results.bin').read_bytes()
    if sha(base/'requests.bin')!=proof['requests_sha256'] or sha(base/'native-results.bin')!=proof['native_results_sha256']:
        raise ValueError('Changed synthetic corpus')
    if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Stale arithmetic/core proof')
    text=(pc/'native/x87_stack.c').read_text()
    boundaries=[('static enum fnv_x87_stack_result calculate(', 'enum fnv_x87_stack_result fnv_x87_stack_binary_pop('),
        ('enum fnv_x87_stack_result fnv_x87_stack_binary_register(', 'enum fnv_x87_stack_result fnv_x87_stack_binary_memory('),
        ('enum fnv_x87_stack_result fnv_x87_stack_binary_memory(', 'static enum fnv_x87_stack_result compare_value(')]
    for start,end in boundaries:
        low=text.index(start);high=text.index(end,low);body=text[low:high]
        if body.count('fnv_x87_calculate(')!=1:raise ValueError('Arithmetic call site changed')
        body=body.replace('fnv_x87_calculate(','fnv_x87_c1_candidate(')
        old='s->status|=(uint16_t)(flags|loaded);' if 'binary_memory(' in start else 's->status|=flags;'
        if body.count(old)!=1:raise ValueError('Arithmetic status site changed')
        body=body.replace(old,'s->status=(uint16_t)((s->status&~0x200u)|flags'+('|loaded' if 'binary_memory(' in start else '')+');')
        text=text[:low]+body+text[high:]
    stack=out/'x87_stack_candidate.c';stack.write_text('#include "x87_c1_candidate.h"\n'+text)
    source=out/'probe.c';source.write_text('''#include "x87_stack.h"
#include "x87_c1_candidate.h"
#include <stddef.h>
void *memcpy(void *d,const void *s,size_t n){unsigned char *a=d;const unsigned char *b=s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;}
void *memset(void *d,int c,size_t n){unsigned char *a=d;for(size_t i=0;i<n;i++)a[i]=(unsigned char)c;return d;}
int memcmp(const void *a,const void *b,size_t n){const unsigned char *x=a,*y=b;for(size_t i=0;i<n;i++)if(x[i]!=y[i])return x[i]>y[i]?1:-1;return 0;}
static unsigned evaluations;
int fnv_counted_original(enum fnv_x87_operation op,uint16_t cw,struct fnv_x87_extended a,struct fnv_x87_extended b,struct fnv_x87_extended *r,uint16_t *f){evaluations++;return fnv_x87_calculate(op,cw,a,b,r,f);}
__attribute__((section(".text.entry"))) int probe(const unsigned char *in,unsigned char *out,unsigned mode){
struct fnv_x87_stack s;fnv_x87_stack_init(&s);s.control=(uint16_t)(in[2]|in[3]<<8);
unsigned method=mode&3,op=in[0];s.top=7;s.occupied=0x80;s.status=0x3a00u|(mode&0x100?0x80:0);
struct fnv_x87_extended a=fnv_x87_unpack(in+4),b=fnv_x87_unpack(in+14);s.registers[7]=a;
if(op!=FNV_X87_SQRT && method==0){s.top=6;s.status=(uint16_t)((s.status&~0x3800u)|0x3000u);s.occupied=0xc0;s.registers[6]=b;}
if(op!=FNV_X87_SQRT && method==1){s.occupied=0x81;s.registers[0]=b;}
struct fnv_x87_stack before;memcpy(&before,&s,sizeof s);evaluations=0;
enum fnv_x87_stack_result result;
if(op==FNV_X87_SQRT)result=fnv_x87_stack_sqrt(&s);
else if(method==0)result=fnv_x87_stack_binary_pop(&s,(enum fnv_x87_operation)op);
else if(method==1)result=fnv_x87_stack_binary_register(&s,(enum fnv_x87_operation)op,0,1,0,0);
else result=fnv_x87_stack_binary_memory(&s,(enum fnv_x87_operation)op,in+24,4,0,0);
fnv_x87_pack(out,s.registers[s.top]);out[10]=(unsigned char)s.status;out[11]=(unsigned char)(s.status>>8);
out[12]=(unsigned char)s.control;out[13]=(unsigned char)(s.control>>8);out[14]=s.occupied;out[15]=s.top;out[16]=(unsigned char)result;
unsigned unchanged=1;struct fnv_x87_stack *old=&before;
for(unsigned i=0;i<8;i++)if(i!=7 && (s.registers[i].significand!=old->registers[i].significand || s.registers[i].sign_exponent!=old->registers[i].sign_exponent))unchanged=0;
out[17]=(unsigned char)unchanged;out[18]=(unsigned char)!memcmp(&s,&before,sizeof s);out[19]=(unsigned char)evaluations;
return 0;}
''')
    includes=['-I'+str(pc/'native'),'-I'+str(pc/'tests')]
    def run(args,log):
        r=subprocess.run([str(v) for v in args],capture_output=True,text=True);(out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr[-4000:])
    obj=out/'stack.o';run([*cc,*core['flags'],*includes,'-c',stack,'-o',obj],'stack.log')
    objects=[Path(o['object']) for o in core['objects'] if Path(o['source']).name!='x87_stack.c']
    candidate=base/'arm-candidate.o';objects.extend([obj,candidate])
    rsp=out/'link.rsp';rsp.write_text('\n'.join('"'+p.as_posix()+'"' for p in objects)+'\n')
    layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    elf=out/'probe.elf';binary=out/'probe.bin'
    run([*cc,'-O1','-fno-builtin','-ffreestanding','-fno-stack-protector','-nostdlib',*includes,source,'@'+str(rsp),'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-o',elf],'link.log')
    run([Path(cc[0]).with_name('aarch64-none-elf-objcopy.exe'),'-O','binary',elf,binary],'objcopy.log')
    u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(0x100000,0x200000);u.mem_write(0x100000,binary.read_bytes());u.mem_map(0x800000,0x10000)
    def execute(record,mode):
        u.mem_write(0x800100,bytes(record));u.mem_write(0x800400,b'\xa5'*28)
        for reg,v in ((ar.UC_ARM64_REG_X0,0x800100),(ar.UC_ARM64_REG_X1,0x800400),(ar.UC_ARM64_REG_X2,mode),(ar.UC_ARM64_REG_SP,0x80f000),(ar.UC_ARM64_REG_LR,0x2ff000)):u.reg_write(reg,v)
        u.emu_start(0x100000,0x2ff000,count=100000)
        if u.reg_read(ar.UC_ARM64_REG_PC)!=0x2ff000 or u.reg_read(ar.UC_ARM64_REG_X0)!=0:raise AssertionError('Candidate stack did not return')
        actual=bytes(u.mem_read(0x800400,28))
        if actual[20:]!=b'\xa5'*8:raise AssertionError('Stack output canary changed')
        return actual
    counts=[0,0,0];failures=[]
    for i in range(len(data)//32):
        record=bytearray(data[i*32:(i+1)*32]);expected=native[i*40+12:i*40+24]
        sig,exp=struct.unpack_from('<QH',record,14);e=(exp&0x7fff)-0x3fff
        f32=(int(bool(exp&0x8000))<<31)|((e+127)<<23)|((sig>>40)&0x7fffff) if sig and -126<=e<=127 and not sig&((1<<40)-1) else 0 if not sig else None
        if f32 is not None:struct.pack_into('<I',record,24,f32)
        for method in range(3):
            if record[0]==4 and method:continue
            if method==2 and f32 is None:continue
            actual=execute(record,method);flags=struct.unpack_from('<H',actual,10)[0];hw=struct.unpack_from('<H',expected,10)[0];mask=0x3f if hw&1 else 0x23f
            if (actual[:10]!=expected[:10] or (flags^hw)&mask or flags&0x3800!=0x3800 or actual[12:14]!=record[2:4] or
                actual[14]!=(0x81 if method==1 else 0x80) or actual[15]!=7 or actual[16]!=0 or not actual[17] or actual[19]!=1):failures.append((i,method))
            counts[method]+=1
    if failures:raise AssertionError(('Candidate stack/native mismatch',failures[:12]))
    negatives=0
    for method in range(3):
        for change in ('operation','precision','unmasked','pending'):
            record=bytearray(data[:32]);mode=method
            if change=='operation':record[0]=255
            if change=='precision':struct.pack_into('<H',record,2,0x17f)
            if change=='unmasked':struct.pack_into('<H',record,2,0x37e)
            if change=='pending':mode|=0x100
            actual=execute(record,mode)
            if not actual[16] or not actual[18]:raise AssertionError('Candidate stack failure changed state')
            negatives+=1
    if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Candidate/base core changed')
    result=dict(status='CANDIDATE_STACK_PASS',register_pop_and_sqrt_cases=counts[0],register_cases=counts[1],memory_cases=counts[2],
                negative_atomic_cases=negatives,stale_c1_replacement_checked=True,production_sources_modified=False,
                proof_sha256=sha(a.arithmetic_proof),private_stack_source_sha256=sha(stack),
                source_sha256={str(p):h for p,h in frozen.items()},hardware_tested=False)
    result['source_sha256'][str(Path(__file__).resolve())]=sha(Path(__file__).resolve())
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('arithmetic-proof','core','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    test(p.parse_args())

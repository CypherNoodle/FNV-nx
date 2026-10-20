"""FSCALE result, flags and unchanged ST(1) versus native x87 and emitted ARM64."""
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
    frame();extended();decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True;disassembler=Disassembler();lifter=Lifter();code=[]
    for decoded in decoder.disasm(bytes.fromhex('db2d10002000db2d00002000d9fd'),0x400000):
        code+=lifter.lift_instruction(disassembler._decode_instruction(decoded))
    assert not lifter.unimplemented and any('fnv_x87_stack_scale' in line for line in code)
    source=out/'oracle.c';source.write_text('''#include "x87_stack.h"
#include <stdio.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
static void oracle(const unsigned char input[24],unsigned char out[24]) {
uint16_t old,cw=(uint16_t)(input[20]|input[21]<<8),sw;memset(out,0,24);
__asm__ volatile("fnstcw %0" : "=m"(old));__asm__ volatile("fninit; fldcw %0" : : "m"(cw));
__asm__ volatile("fldt %1; fldt %0; fscale; fnstsw 20(%%rdi); fstpt (%%rdi); fstpt 10(%%rdi)" : :
"m"(*(const unsigned char (*)[10])input),"m"(*(const unsigned char (*)[10])(input+10)),"D"(out) : "memory","st","st(1)");
sw=(uint16_t)(out[20]|out[21]<<8);sw&=0x3a3f;out[20]=(unsigned char)sw;out[21]=(unsigned char)(sw>>8);out[22]=0xc0;out[23]=6;
__asm__ volatile("fnclex; fldcw %0" : : "m"(old));
}
int main(void) {
if(_setmode(_fileno(stdin),_O_BINARY)<0 || _setmode(_fileno(stdout),_O_BINARY)<0)return 1;
unsigned char input[24];unsigned count=0;
for(;;){size_t n=fread(input,1,24,stdin);if(!n)break;if(n!=24)return 2;
unsigned char expected[24],actual[24]={0};oracle(input,expected);
struct fnv_x87_stack s;fnv_x87_stack_init(&s);s.control=(uint16_t)(input[20]|input[21]<<8);
if(fnv_x87_stack_load(&s,input+10)!=FNV_X87_STACK_OK || fnv_x87_stack_load(&s,input)!=FNV_X87_STACK_OK)return 3;
if(fnv_x87_stack_scale(&s)!=FNV_X87_STACK_OK)return 4;
fnv_x87_pack(actual,s.registers[s.top]);fnv_x87_pack(actual+10,s.registers[(s.top+1)&7]);
actual[20]=(unsigned char)(s.status&0x3f);actual[21]=(unsigned char)((s.status&0x3a00)>>8);actual[22]=s.occupied;actual[23]=s.top;
if(memcmp(actual,expected,24)){fprintf(stderr,"Mismatch case %u cw=%04x\\n",count,s.control);for(unsigned i=0;i<24;i++)fprintf(stderr,"%02x",actual[i]);fputc('\\n',stderr);for(unsigned i=0;i<24;i++)fprintf(stderr,"%02x",expected[i]);fputc('\\n',stderr);return 5;}
if(fwrite(expected,1,24,stdout)!=24)return 6;count++;}
fprintf(stderr,"PASS %u native FSCALE cases\\n",count);return ferror(stdin)?7:0;
}
''')
    def response(proof,name):
        path=out/name;path.write_text('\n'.join('"'+e['object'].replace('\\','/')+'"' for e in proof['objects'])+'\n');return path
    def compile(command,log):
        r=subprocess.run(command,capture_output=True,text=True);(out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr[-3000:] or str(r.returncode))
    exe=out/'oracle.exe';compile([*native['compiler'],'-O2','-I'+str(pc/'native'),str(source),'@'+str(response(native,'native.rsp')),'-Wl,--gc-sections','-o',str(exe)],'native-compiler.log')
    def raw(sig,exponent):return struct.pack('<QH',sig,exponent)
    def dyadic(n,scale=0):
        if not n:return bytes(10)
        sign=0x8000 if n<0 else 0;n=abs(n);bits=n.bit_length();assert bits<=64
        return raw(n<<(64-bits),sign|((bits-1-scale)+0x3fff))
    values=[dyadic(0),raw(0,0x8000),dyadic(1),dyadic(-1),raw(0x8000000000000001,0x3fff),
        raw(0xffffffffffffffff,0x7ffe),raw(1<<63,0x7fff),raw(1<<63,0xffff),raw(0xc000000000001234,0x7fff)]
    scales=[dyadic(n,s) for n,s in ((0,0),(1,1),(-1,1),(1,0),(-1,0),(3,1),(-3,1),(32768,0),(-32768,0),(65536,0),(-65536,0),(1,64),(-1,64))]
    scales.extend([raw(1<<63,0x7fff),raw(1<<63,0xffff),raw(0xc000000000005678,0x7fff)])
    pairs=[(v,s) for v in values for s in scales]
    for exp in (1,2,0x7ffd,0x7ffe):
        for sig in (1<<63,(1<<63)+1,0xffffffffffffffff):
            for sign in (0,0x8000):
                for scale in (-65,-64,-63,-2,-1,0,1,2,63,64,65):pairs.append((raw(sig,exp|sign),dyadic(scale)))
    rng=random.Random(0xf5ca1e)
    pairs.extend((raw(rng.getrandbits(63)|(1<<63),rng.randint(1,0x7ffe)|(rng.randrange(2)<<15)),dyadic(rng.randint(-65536,65536),rng.randint(0,16))) for _ in range(128))
    records=[v+s+struct.pack('<HH',0x7f|(precision<<8)|(rounding<<10),0) for precision in (0,2,3) for rounding in range(4) for v,s in pairs]
    (out/'inputs.bin').write_bytes(b''.join(records));r=subprocess.run([str(exe)],input=b''.join(records),capture_output=True);(out/'oracle.log').write_bytes(r.stderr)
    if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace'))
    expected=r.stdout;assert len(expected)==len(records)*24;(out/'expected.bin').write_bytes(expected)
    entry=out/'entry.c';entry.write_text('''#include <stddef.h>
#include "x87_stack.h"
__attribute__((section(".text.entry"))) void probe(const unsigned char *request,unsigned char *out) {
struct fnv_x87_stack fnv_x87_state;fnv_x87_stack_init(&fnv_x87_state);fnv_x87_state.control=(uint16_t)(request[20]|request[21]<<8);
unsigned char guest[32]={0};for(unsigned i=0;i<10;i++){guest[i]=request[i];guest[16+i]=request[10+i];}
ptrdiff_t g_xbox_mem_offset=(ptrdiff_t)guest-0x200000;
#define RECOMP_UNIMPL(text,va) do {out[0]=255;return;} while(0)
'''+ '\n'.join(code)+'''
fnv_x87_pack(out,fnv_x87_state.registers[fnv_x87_state.top]);fnv_x87_pack(out+10,fnv_x87_state.registers[(fnv_x87_state.top+1)&7]);
out[20]=(unsigned char)(fnv_x87_state.status&0x3f);out[21]=(unsigned char)((fnv_x87_state.status&0x3a00)>>8);out[22]=fnv_x87_state.occupied;out[23]=fnv_x87_state.top;
}
''')
    layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    elf=out/'probe.elf';cc=arm_proof['compiler'];compile([*cc,'-O2','-ffreestanding','-fno-stack-protector','-nostdlib','-I'+str(pc/'native'),str(entry),'@'+str(response(arm_proof,'arm.rsp')),'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-o',str(elf)],'arm-compiler.log')
    binary=out/'probe.bin';subprocess.run([str(Path(cc[0]).resolve().with_name('aarch64-none-elf-objcopy.exe')),'-O','binary',str(elf),str(binary)],check=True)
    arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x200000);arm.mem_write(0x100000,binary.read_bytes());arm.mem_map(0x800000,0x10000)
    for index,record in enumerate(records):
        arm.mem_write(0x800100,record);arm.mem_write(0x800200,b'\xa5'*28)
        arm.reg_write(ar.UC_ARM64_REG_X0,0x800100);arm.reg_write(ar.UC_ARM64_REG_X1,0x800200);arm.reg_write(ar.UC_ARM64_REG_SP,0x80f000);arm.reg_write(ar.UC_ARM64_REG_LR,0x2ff000)
        arm.emu_start(0x100000,0x2ff000,count=20000)
        assert arm.reg_read(ar.UC_ARM64_REG_PC)==0x2ff000,index
        assert bytes(arm.mem_read(0x800200,28))==expected[index*24:index*24+24]+b'\xa5'*4,index
    verify(native);verify(arm_proof)
    paths=[Path(__file__).resolve(),Path(__file__).with_name('test_x87_integer_compare.py'),a.build.resolve(),a.arm_build.resolve(),pc/'tools/aot_x87_extended.py',pc/'tools/aot_frame_restore.py']
    result=dict(status='PASS',native_cases=len(records),arm64_translated_cases=len(records),source_sha256={str(p):sha(p) for p in paths},sdk_sha256=sdk_identity(a.sdk.resolve()),switch_hardware_tested=False,
        limits=['Masked canonical operands; extended-denormal and signaling-NaN source operands and stack faults remain unsupported'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('build','arm-build','sdk','python-deps','output'):parser.add_argument('--'+name,type=Path,required=True)
    test(parser.parse_args())

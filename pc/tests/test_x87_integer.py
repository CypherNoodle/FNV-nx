"""Differential FILD/FIST/FISTP/FISTTP/FRNDINT/FBSTP versus native x87."""
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
    source=Path(__file__).with_name('x87_integer_host.c').resolve()
    def response(proof,name):
        p=out/name;p.write_text('\n'.join('"'+e['object'].replace('\\','/')+'"' for e in proof['objects'])+'\n');return p
    exe=out/'oracle.exe'
    r=subprocess.run([*native['compiler'],'-O2','-I'+str(pc/'native'),str(source),'@'+str(response(native,'native.rsp')),'-Wl,--gc-sections','-o',str(exe)],capture_output=True,text=True)
    (out/'native-compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr[-2500:])
    def dyadic(n,scale=0):
        if not n:return bytes(10)
        sign=0x8000 if n<0 else 0;n=abs(n);bits=n.bit_length();assert bits<=64
        return struct.pack('<QH',n<<(64-bits),sign|((bits-1-scale)+0x3fff))
    values=[dyadic(n,s) for n,s in ((0,0),(1,0),(-1,0),(1,1),(-1,1),(3,1),(-3,1),
        (65535,1),(-65537,1),(4294967295,1),(-4294967297,1),((1<<64)-1,1),
        ((1<<63)+1,0),(-(1<<63),0),(-(1<<63)-1,0))]
    values.extend([struct.pack('<QH',1<<63,0x7fff),struct.pack('<QH',0xc000000000001234,0x7fff)])
    values.extend(dyadic(n,s) for n,s in ((10**18-1,0),(-(10**18-1),0),(10**18,0),(-10**18,0),(2*10**18-1,1),(-(2*10**18-1),1)))
    integers=[0,1,-1,32767,32768,-32768,-32769,(1<<31)-1,1<<31,-(1<<31),
              (1<<53)+1,(1<<63)-1,-(1<<63)]
    rng=random.Random(0x87f157)
    for _ in range(60):
        values.append(dyadic(rng.randint(-(1<<62),(1<<62)),rng.randint(0,64)))
        integers.append(rng.randint(-(1<<63),(1<<63)-1))
    records=[]
    for pc_bits in (0,2,3):
        for rc in range(4):
            cw=0x7f|(pc_bits<<8)|(rc<<10)
            for mode in range(13):
                for index,value in enumerate(values):
                    integer=integers[index%len(integers)]
                    records.append(value+struct.pack('<QH',integer&((1<<64)-1),0)+struct.pack('<HBB',cw,mode,0))
    data=b''.join(records);(out/'inputs.bin').write_bytes(data)
    r=subprocess.run([str(exe)],input=data,capture_output=True)
    (out/'oracle.log').write_bytes(r.stderr);(out/'expected.bin').write_bytes(r.stdout)
    if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace'))
    assert len(r.stdout)==len(records)*12
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
    bodies=[];instructions=0
    for mode in range(13):
        if mode<3:
            raw=(b'\xdf\x05',b'\xdb\x05',b'\xdf\x2d')[mode]+b'\x10\x00\x20\x00'+b'\xdb\x3d\x20\x00\x20\x00'
        else:
            prefix=b'\xdb\x2d\x00\x00\x20\x00'
            if mode==9:raw=prefix+b'\xd9\xfc\xdb\x3d\x20\x00\x20\x00'
            else:
                opcode={3:b'\xdf\x1d',4:b'\xdb\x1d',5:b'\xdf\x3d',6:b'\xdf\x0d',7:b'\xdb\x0d',8:b'\xdd\x0d',10:b'\xdf\x15',11:b'\xdb\x15',12:b'\xdf\x35'}[mode]
                raw=prefix+opcode+b'\x20\x00\x20\x00'+(b'\xdd\xd8' if mode in (10,11) else b'')
        lifter=Lifter();lines=[]
        for decoded in decoder.disasm(raw,0x400000+mode*32):
            lines+=lifter.lift_instruction(disassembler._decode_instruction(decoded));instructions+=1
        assert not lifter.unimplemented
        bodies.append(f'case {mode}: '+'{\n'+'\n'.join(lines)+'\nbreak;}')
    entry=out/'entry.c';entry.write_text('''#include <stddef.h>
#include "x87_stack.h"
__attribute__((section(".text.entry"))) void probe(const unsigned char *request,unsigned char *out) {
struct fnv_x87_stack fnv_x87_state;fnv_x87_stack_init(&fnv_x87_state);
fnv_x87_state.control=(uint16_t)(request[20]|request[21]<<8);
unsigned char guest[48]={0};for(unsigned i=0;i<10;i++){guest[i]=request[i];guest[16+i]=request[10+i];}
ptrdiff_t g_xbox_mem_offset=(ptrdiff_t)guest-0x200000;
#define RECOMP_UNIMPL(text,va) do {out[0]=255;return;} while(0)
switch(request[22]) {
'''+ '\n'.join(bodies)+'''
}
for(unsigned i=0;i<10;i++)out[i]=guest[32+i];out[10]=(unsigned char)(fnv_x87_state.status&0x3f);out[11]=request[22]==12 ? (unsigned char)((fnv_x87_state.status&0x200)>>8) : 0;
if(fnv_x87_state.top || fnv_x87_state.occupied)out[0]=254;
}
''')
    layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    cc=arm_proof['compiler'];elf=out/'probe.elf'
    r=subprocess.run([*cc,'-O2','-ffreestanding','-fno-stack-protector','-nostdlib','-I'+str(pc/'native'),str(entry),'@'+str(response(arm_proof,'arm.rsp')),'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-o',str(elf)],capture_output=True,text=True)
    (out/'arm-compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr[-2500:])
    binary=out/'probe.bin';subprocess.run([str(Path(cc[0]).resolve().with_name('aarch64-none-elf-objcopy.exe')),'-O','binary',str(elf),str(binary)],check=True)
    arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x200000);arm.mem_write(0x100000,binary.read_bytes());arm.mem_map(0x800000,0x10000)
    expected=(out/'expected.bin').read_bytes()
    for index,record in enumerate(records):
        arm.mem_write(0x800100,record);arm.mem_write(0x800200,b'\xa5'*16)
        arm.reg_write(ar.UC_ARM64_REG_X0,0x800100);arm.reg_write(ar.UC_ARM64_REG_X1,0x800200)
        arm.reg_write(ar.UC_ARM64_REG_SP,0x80f000);arm.reg_write(ar.UC_ARM64_REG_LR,0x2ff000)
        arm.emu_start(0x100000,0x2ff000,count=10000)
        assert arm.reg_read(ar.UC_ARM64_REG_PC)==0x2ff000,index
        assert bytes(arm.mem_read(0x800200,16))==expected[index*12:index*12+12]+b'\xa5'*4,index
    verify(native);verify(arm_proof)
    paths=[Path(__file__).resolve(),source,a.build.resolve(),a.arm_build.resolve(),pc/'tools/aot_x87_extended.py',pc/'tools/aot_frame_restore.py']
    result=dict(status='PASS',native_cases=len(records),arm64_translated_cases=len(records),translated_instruction_instances=instructions,
                bcd_cases=12*len(values),bcd_c1_checked=True,
                source_sha256={str(p):sha(p) for p in paths},sdk_sha256=sdk_identity(a.sdk.resolve()),
                arm64_binary_sha256=sha(binary),runtime_integrated=False,switch_hardware_tested=False,
                limits=['Masked canonical operands; C1 checked for FBSTP only; extended-denormal source operands and exception delivery excluded'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','arm-build','sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    test(p.parse_args())

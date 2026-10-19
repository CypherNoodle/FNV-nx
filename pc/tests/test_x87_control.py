"""Compare extended FCOM/FUCOM/control/status translations with actual x87."""
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
    source=Path(__file__).with_name('x87_control_host.c').resolve()
    def response(proof,name):
        p=out/name;p.write_text('\n'.join('"'+e['object'].replace('\\','/')+'"' for e in proof['objects'])+'\n');return p
    exe=out/'oracle.exe'
    r=subprocess.run([*native['compiler'],'-O2','-I'+str(pc/'native'),str(source),'@'+str(response(native,'native.rsp')),'-Wl,--gc-sections','-o',str(exe)],capture_output=True,text=True)
    (out/'native-compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr[-2500:])
    def value(sig,exp):return struct.pack('<QH',sig,exp)
    one=value(1<<63,0x3fff);zero=value(0,0);negative_zero=value(0,0x8000)
    nan=value(0xc000000000001234,0x7fff)
    pairs=[(one,one),(one,value((1<<63)+1,0x3fff)),(value((1<<63)+1,0x3fff),one),
           (zero,negative_zero),(negative_zero,zero),(nan,one),(one,nan),(nan,nan),
           (value(1<<63,0x7fff),one),(one,value(1<<63,0xffff))]
    pairs.extend([(one,value(0xfff000017f800001,0x3fff)),(one,value(0xc000000000000001,0x7fff)),
                  (one,value(0x8000000000000001,0x3fff))])
    rng=random.Random(0x87c0)
    for _ in range(80):pairs.append(tuple(value((1<<63)|rng.getrandbits(63),rng.randint(1,0x7ffe)|(rng.randrange(2)<<15)) for _ in range(2)))
    records=[]
    for pc_bits in (0,2,3):
        for rc in range(4):
            for ic in (0,1):
                cw=0x7f|(pc_bits<<8)|(rc<<10)|(ic<<12)
                for mode in range(13):
                    for x,y in pairs:records.append(x+y+struct.pack('<HBB',cw,mode,0))
    data=b''.join(records);(out/'inputs.bin').write_bytes(data)
    r=subprocess.run([str(exe)],input=data,capture_output=True)
    (out/'oracle.log').write_bytes(r.stderr);(out/'expected.bin').write_bytes(r.stdout)
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
    bodies=[];instructions=0
    modes=[b'\xde\xd9',b'\xda\xe9',b'\xd8\xd9\xdd\xd8',b'\xdd\xe9\xdd\xd8',b'\xde\xd9\xdb\xe2',b'\xde\xd9\xdb\xe3',
           b'\xd8\x15\x10\x00\x20\x00\xdd\xd8\xdd\xd8',b'\xd8\x1d\x10\x00\x20\x00\xdd\xd8',
           b'\xdc\x15\x10\x00\x20\x00\xdd\xd8\xdd\xd8',b'\xdc\x1d\x10\x00\x20\x00\xdd\xd8',
           b'\xd9\xe4\xdd\xd8\xdd\xd8',b'\xd9\xe0\xdb\x3d\x38\x00\x20\x00\xdd\xd8',
           b'\xd9\xe1\xdb\x3d\x38\x00\x20\x00\xdd\xd8']
    for mode,rawmode in enumerate(modes):
        raw=b'\xdb\xe3\xd9\x2d\x30\x00\x20\x00'+b'\xdb\x2d\x00\x00\x20\x00\xdb\x2d\x10\x00\x20\x00'+rawmode+b'\xdf\xe0\xdd\x3d\x32\x00\x20\x00\xd9\x3d\x34\x00\x20\x00'
        lifter=Lifter();lines=[]
        for decoded in decoder.disasm(raw,0x400000+mode*0x100):
            lines+=lifter.lift_instruction(disassembler._decode_instruction(decoded));instructions+=1
        assert not lifter.unimplemented
        bodies.append(f'case {mode}: '+'{\n'+'\n'.join(lines)+'\nbreak;}')
    entry=out/'entry.c';entry.write_text('''#include <stddef.h>
#include "x87_stack.h"
__attribute__((section(".text.entry"))) void probe(const unsigned char *request,unsigned char *out) {
struct fnv_x87_stack fnv_x87_state;unsigned char guest[80]={0};uint32_t eax=0xa5a5feedu;
for(unsigned i=0;i<10;i++){guest[i]=request[i];guest[16+i]=request[10+i];}
guest[48]=request[20];guest[49]=request[21];
ptrdiff_t g_xbox_mem_offset=(ptrdiff_t)guest-0x200000;
#define RECOMP_UNIMPL(text,va) do {out[0]=255;return;} while(0)
switch(request[22]) {
'''+ '\n'.join(bodies)+'''
}
out[0]=guest[50];out[1]=guest[51];for(unsigned i=0;i<4;i++)out[2+i]=(unsigned char)(eax>>(i*8));
out[6]=guest[52];out[7]=guest[53];if(fnv_x87_state.top || fnv_x87_state.occupied)out[0]=254;
for(unsigned i=0;i<10;i++)out[8+i]=guest[56+i];
if(request[22]>=11){out[1]&=0xba;out[3]&=0xba;} /* Undefined sign-op C0/C2/C3 excluded. */
}
''')
    layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    arm_cc=arm_proof['compiler'];elf=out/'probe.elf'
    r=subprocess.run([*arm_cc,'-O2','-ffreestanding','-fno-stack-protector','-nostdlib','-I'+str(pc/'native'),str(entry),'@'+str(response(arm_proof,'arm.rsp')),'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-o',str(elf)],capture_output=True,text=True)
    (out/'arm-compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr[-2500:])
    binary=out/'probe.bin'
    subprocess.run([str(Path(arm_cc[0]).resolve().with_name('aarch64-none-elf-objcopy.exe')),'-O','binary',str(elf),str(binary)],check=True)
    arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x200000);arm.mem_write(0x100000,binary.read_bytes());arm.mem_map(0x800000,0x10000)
    expected=(out/'expected.bin').read_bytes()
    for index,record in enumerate(records):
        arm.mem_write(0x800100,record);arm.mem_write(0x800200,b'\xa5'*22)
        arm.reg_write(ar.UC_ARM64_REG_X0,0x800100);arm.reg_write(ar.UC_ARM64_REG_X1,0x800200)
        arm.reg_write(ar.UC_ARM64_REG_SP,0x80f000);arm.reg_write(ar.UC_ARM64_REG_LR,0x2ff000)
        arm.emu_start(0x100000,0x2ff000,count=10000)
        assert arm.reg_read(ar.UC_ARM64_REG_PC)==0x2ff000,index
        assert bytes(arm.mem_read(0x800200,22))==expected[index*18:index*18+18]+b'\xa5'*4,index
    verify(native);verify(arm_proof)
    inputs=[Path(__file__).resolve(),source,a.build.resolve(),a.arm_build.resolve(),pc/'tools/aot_x87_extended.py',pc/'tools/aot_frame_restore.py']
    result=dict(status='PASS',native_cases=len(records),arm64_translated_cases=len(records),translated_instruction_instances=instructions,
                source_sha256={str(p):sha(p) for p in inputs},sdk_sha256=sdk_identity(a.sdk.resolve()),
                arm64_binary_sha256=sha(binary),runtime_integrated=False,switch_hardware_tested=False,
                limits=['Canonical masked operands only','Undefined FCHS/FABS C0/C2/C3 excluded',
                        'No FCOMI/EFLAGS, C1 arithmetic, extended denormal operands or exception delivery'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','arm-build','sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    test(p.parse_args())

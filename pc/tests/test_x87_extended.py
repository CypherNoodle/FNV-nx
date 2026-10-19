"""Differential extended arithmetic core versus actual x86 x87 instructions.

Requires a completed native build from build_x87_extended.py. Inputs are
synthetic canonical normal/zero/infinity/NaN values, never game data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    proof=json.loads(a.build.read_text())
    assert proof['status']=='COMPILED'
    assert all(sha(Path(p))==h for p,h in proof['source_sha256'].items())
    assert all(sha(Path(p['object']))==p['sha256'] for p in proof['objects'])
    compiler=proof['compiler'];source=Path(__file__).with_name('x87_extended_host.c').resolve()
    exe=out/'oracle.exe'
    rsp=out/'link.rsp';rsp.write_text('\n'.join('"'+i['object'].replace('\\','/')+'"' for i in proof['objects'])+'\n')
    r=subprocess.run([*compiler,'-O2','-I'+str(pc/'native'),str(source),'@'+str(rsp),'-Wl,--gc-sections','-o',str(exe)],capture_output=True,text=True)
    (out/'compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr[-3000:])
    def value(sig,exp,sign=0):return struct.pack('<QH',sig,exp|(sign<<15))
    rng=random.Random(0x80387)
    one=value(1<<63,0x3fff);half_ulp=value(1<<63,0x3fc0)
    pairs=[(one,half_ulp),(one,one),(one,value(1<<63,0x4000)),
           (value(0,0),value(0,0)),(value(0,0,1),one),
           (value((1<<64)-1,0x7ffe),one),(value(1<<63,1),value(1<<63,0x4000)),
           (value(1<<63,0x7fff),one),(value(0xc000000000001234,0x7fff),one)]
    pairs.extend([(one,value(0,0)),(value(0,0),value(1<<63,0x7fff)),
                  (value((1<<64)-1,0x7ffe),value(1<<63,0x4000)),
                  (value(1<<63,1),value(1<<63,1)),
                  (value(0xc000000000001234,0x7fff),value(0xc000000000005678,0x7fff))])
    # Halfway cases at the normal/subnormal boundary of binary32/binary64.
    for exponent,halfway in ((0x3f80,1<<40),(0x3c00,1<<11)):
        for delta in (-1,0,1):
            for sign in (0,1):pairs.append((value((1<<64)-halfway+delta,exponent,sign),one))
    # Reinterpret these payloads in conversion tests, including both signs.
    for bits in (1,0x007fffff,0x00800000,0x7f7fffff,0x7f800000,0x7f800001,0x7fc00001,
                 0x8000000000000001,0x800fffffffffffff,0x8010000000000000,
                 0xffefffffffffffff,0xfff0000000000000,0xfff0000000000001,0xfff8000000000001):
        pairs.append((value(bits|(1<<63),0x3fff),one))
    for _ in range(160):
        pairs.append(tuple(value(rng.getrandbits(63)|(1<<63),rng.randint(0x1000,0x6fff),rng.randrange(2)) for _ in range(2)))
    records=[]
    for pc_bits in (0,2,3):
        for rc in range(4):
            cw=0x7f|(pc_bits<<8)|(rc<<10)
            for op in range(5):
                for av,bv in pairs:records.append(av+bv+struct.pack('<HBB',cw,op,0))
    data=b''.join(records);(out/'inputs.bin').write_bytes(data)
    r=subprocess.run([str(exe)],input=data,capture_output=True)
    (out/'oracle.log').write_bytes(r.stderr)
    (out/'expected.bin').write_bytes(r.stdout)
    if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace'))
    assert len(r.stdout)==len(records)*124
    assert b'PASS 40000 concurrent arithmetic calls' in r.stderr
    assert b'PASS context copies, stack capacity and non-writing failure paths' in r.stderr
    # At 64-bit significand precision, 1 + 2^-63 must survive. Binary64 loses it.
    index=(2*4*5)*len(pairs)
    assert r.stdout[index*124:index*124+10]==value((1<<63)+1,0x3fff)
    assert all(sha(Path(p))==h for p,h in proof['source_sha256'].items())
    result=dict(status='PASS',native_x87_cases=len(records),operations=5,rounding_modes=4,
                concurrent_calls=40000,invalid_call_checks=4,conversion_cases=len(records)*6,
                register_sequence_cases=len(records)*4,
                context_copy_and_failure_checks=True,
                precision_controls=[24,53,64],extended_precision_regression=True,
                inputs_sha256=sha(out/'inputs.bin'),expected_sha256=sha(out/'expected.bin'),
                source_sha256={str(p):sha(p) for p in (Path(__file__).resolve(),source,a.build.resolve())},
                runtime_integrated=False,switch_hardware_tested=False,
                limits=['Arithmetic extended-denormal operands/C1, tags and guest exception dispatch excluded',
                        'Binary32/64 denormal-load flags covered; extended pseudo/unnormal encodings excluded',
                        'Transcendental operations excluded'])
    if a.arm_build:
        import sys
        sys.path.insert(0,str(a.python_deps.resolve()))
        from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
        import unicorn.arm64_const as ar
        sys.path[:0]=[str(a.sdk.resolve()),str(pc/'tools')]
        from capstone import Cs,CS_ARCH_X86,CS_MODE_32
        from tools.recomp.disasm import Disassembler
        from tools.recomp.lifter import Lifter
        from aot_frame_restore import install as frame
        from aot_x87_extended import install as extended
        from run_aot_analysis import sdk_identity
        frame();extended();installed=Lifter.lift_instruction;extended();assert installed is Lifter.lift_instruction
        decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True;disassembler=Disassembler()
        bodies=[];translated=0
        for op,rawop in enumerate((b'\xde\xc1',b'\xde\xe9',b'\xde\xc9',b'\xde\xf9',b'\xd9\xfa')):
            raw=b'\xdb\x2d\x00\x00\x20\x00'
            if op!=4:raw+=b'\xdb\x2d\x10\x00\x20\x00'
            raw+=rawop+b'\xdb\x3d\x20\x00\x20\x00'
            lifter=Lifter();lines=[]
            for decoded in decoder.disasm(raw,0x400000):
                lines+=lifter.lift_instruction(disassembler._decode_instruction(decoded));translated+=1
            assert not lifter.unimplemented
            bodies.append(f'case {op}: '+'{\n'+'\n'.join(lines)+'\nbreak;}')
        # A form using the old double stack must never silently fall through.
        lifter=Lifter();ins=disassembler._decode_instruction(next(decoder.disasm(b'\xd9\xfe',0x500000)))
        assert 'RECOMP_UNIMPL' in '\n'.join(lifter.lift_instruction(ins)) and lifter.unimplemented
        conversions=[];offset=12
        for conversion in range(6):
            width=8 if conversion%2 else 4;length=10 if conversion>=2 else width
            if conversion>=2:
                raw=bytes([0xdd if width==8 else 0xd9,0x05])+b'\x00\x00\x20\x00'+b'\xdb\x3d\x20\x00\x20\x00'
            else:
                raw=b'\xdb\x2d\x00\x00\x20\x00'+bytes([0xdd if width==8 else 0xd9,0x1d])+b'\x20\x00\x20\x00'
            lifter=Lifter();lines=[]
            for decoded in decoder.disasm(raw,0x400100+conversion*16):
                lines+=lifter.lift_instruction(disassembler._decode_instruction(decoded));translated+=1
            assert not lifter.unimplemented
            conversions.append('fnv_x87_stack_init(&fnv_x87_state);fnv_x87_state.control=(uint16_t)(request[20]|request[21]<<8);\n'+
                (f'guest[{width-1}]^=0x80u;\n' if conversion>=4 else '')+
                '\n'.join(lines)+f'\nfor(unsigned i=0;i<{length};i++)out[{offset}+i]=guest[32+i];\n'+
                f'out[{offset+length}]=(unsigned char)(fnv_x87_state.status&0x3f);out[{offset+length+1}]=0;\n'+
                (f'guest[{width-1}]^=0x80u;\n' if conversion>=4 else '')+
                'if(fnv_x87_state.top || fnv_x87_state.occupied){out[0]=251;return;}')
            offset+=length+2
        assert offset==76
        for sequence,body in enumerate((b'\xd9\xc0\xde\xc1',b'\xdb\x2d\x10\x00\x20\x00\xd9\xc9\xdd\xd1\xde\xc1',
                                        b'\xdd\xd8\xd9\xe8',b'\xdd\xd8\xd9\xee')):
            raw=b'\xdb\x2d\x00\x00\x20\x00'+body+b'\xdb\x3d\x20\x00\x20\x00'
            lifter=Lifter();lines=[]
            for decoded in decoder.disasm(raw,0x400200+sequence*32):
                lines+=lifter.lift_instruction(disassembler._decode_instruction(decoded));translated+=1
            assert not lifter.unimplemented
            conversions.append('fnv_x87_stack_init(&fnv_x87_state);fnv_x87_state.control=(uint16_t)(request[20]|request[21]<<8);\n'+
                '\n'.join(lines)+f'\nfor(unsigned i=0;i<10;i++)out[{offset}+i]=guest[32+i];\n'+
                f'out[{offset+10}]=(unsigned char)(fnv_x87_state.status&0x3f);out[{offset+11}]=0;\n'+
                'if(fnv_x87_state.top || fnv_x87_state.occupied){out[0]=250;return;}')
            offset+=12
        assert offset==124
        arm_proof=json.loads(a.arm_build.read_text())
        assert arm_proof['status']=='COMPILED'
        assert all(sha(Path(p))==h for p,h in arm_proof['source_sha256'].items())
        assert all(sha(Path(p['object']))==p['sha256'] for p in arm_proof['objects'])
        entry=out/'arm_entry.c'
        entry.write_text('''#include <stddef.h>
#include "x87_extended.h"
#include "x87_stack.h"
__attribute__((section(".text.entry"))) void probe(const unsigned char *request,unsigned char *out) {
struct fnv_x87_stack fnv_x87_state;fnv_x87_stack_init(&fnv_x87_state);
fnv_x87_state.control=(uint16_t)(request[20]|request[21]<<8);
unsigned char guest[48];
for(unsigned i=0;i<10;i++){guest[i]=request[i];guest[16+i]=request[10+i];}
ptrdiff_t g_xbox_mem_offset=(ptrdiff_t)guest-0x200000;
#define RECOMP_UNIMPL(text,va) do {out[0]=255;out[1]=(unsigned char)(va);return;} while(0)
switch(request[22]) {
'''+ '\n'.join(bodies)+'''
}
if(fnv_x87_state.top || fnv_x87_state.occupied) {out[0]=252;return;}
for(unsigned i=0;i<10;i++)out[i]=guest[32+i];
out[10]=(unsigned char)(fnv_x87_state.status&0x3d);out[11]=0;
'''+ '\n'.join(conversions)+'''
}
''')
        layout=out/'layout.ld'
        layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
        rsp=out/'arm.rsp';rsp.write_text('\n'.join('"'+i['object'].replace('\\','/')+'"' for i in arm_proof['objects'])+'\n')
        elf=out/'arm.elf';arm_cc=arm_proof['compiler']
        r=subprocess.run([*arm_cc,'-O2','-ffreestanding','-fno-stack-protector','-nostdlib',
            '-I'+str(pc/'native'),'-Wl,--gc-sections','-Wl,-T,'+str(layout),str(entry),'@'+str(rsp),'-o',str(elf)],capture_output=True,text=True)
        (out/'arm-compiler.log').write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr[-3000:])
        binary=out/'arm.bin'
        subprocess.run([str(Path(arm_cc[0]).resolve().with_name('aarch64-none-elf-objcopy.exe')),'-O','binary',str(elf),str(binary)],check=True)
        arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x200000)
        arm.mem_write(0x100000,binary.read_bytes());arm.mem_map(0x800000,0x10000)
        expected=(out/'expected.bin').read_bytes()
        for index,record in enumerate(records):
            arm.mem_write(0x800100,record);arm.mem_write(0x800200,bytes([0xa5])*128)
            arm.reg_write(ar.UC_ARM64_REG_X0,0x800100);arm.reg_write(ar.UC_ARM64_REG_X1,0x800200)
            arm.reg_write(ar.UC_ARM64_REG_SP,0x80f000);arm.reg_write(ar.UC_ARM64_REG_LR,0x2ff000)
            arm.emu_start(0x100000,0x2ff000,count=50000)
            assert arm.reg_read(ar.UC_ARM64_REG_PC)==0x2ff000, index
            assert bytes(arm.mem_read(0x800200,128))==expected[index*124:index*124+124]+bytes([0xa5])*4,index
        result['arm64_differential_cases']=len(records)
        result['stack_roundtrips']=len(records)
        result['translated_instruction_forms']=translated
        result['sdk_sha256']=sdk_identity(a.sdk.resolve())
        result['arm64_binary_sha256']=sha(binary)
        result['source_sha256'][str(a.arm_build.resolve())]=sha(a.arm_build)
        for name in ('aot_x87_extended.py','aot_frame_restore.py'):
            result['source_sha256'][str(pc/'tools'/name)]=sha(pc/'tools'/name)
        assert all(sha(Path(p))==h for p,h in arm_proof['source_sha256'].items())
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--arm-build',type=Path);p.add_argument('--python-deps',type=Path)
    p.add_argument('--sdk',type=Path)
    a=p.parse_args()
    if a.arm_build and (not a.python_deps or not a.sdk):p.error('--arm-build requires --python-deps and --sdk')
    test(a)

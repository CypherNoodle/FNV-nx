"""Differential logical shifts: x86 instructions versus compiled ARM64.

Tests values, unaffected register/memory bytes, zero-count tracking and defined
CF. Undefined CF/OF and other incoming-EFLAGS reconstruction are not claimed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys


def test(a):
    pc=Path(__file__).resolve().parents[1]
    sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve()),str(pc/'tools')]
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    from tools.recomp.disasm import Disassembler
    from tools.recomp.lifter import Lifter
    from aot_frame_restore import install
    from run_aot_analysis import sdk_identity,sha
    from unicorn import Uc,UC_ARCH_X86,UC_ARCH_ARM64,UC_MODE_32,UC_MODE_ARM
    import unicorn.x86_const as xr
    import unicorn.arm64_const as ar
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();sdk=a.sdk.resolve()
    identity=sdk_identity(sdk);inputs={str(p.resolve()):sha(p) for p in (Path(__file__),pc/'tools/aot_frame_restore.py',a.cc)}
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True;model=Disassembler()
    original=Lifter._lift_shift
    install();installed=Lifter._lift_shift;install();assert installed is Lifter._lift_shift
    forms=[];blocks=[]
    for width in (1,2,4):
        for memory in (False,True):
            for right in (False,True):
                for immediate in (None,0,1,31,32,33,230,255):
                    raw=(b'\x66' if width==2 else b'')+bytes([
                        (0xd2 if width==1 else 0xd3) if immediate is None else (0xc0 if width==1 else 0xc1),
                        (0x20 if memory else 0xe3)+(8 if right else 0)])
                    if immediate is not None:raw+=bytes([immediate])
                    ins=model._decode_instruction(next(decoder.disasm(raw,0x400000)))
                    lifter=Lifter();lifter.needs_cf=True
                    code=lifter.lift_instruction(ins);assert not lifter.unimplemented
                    if width==1 and not memory and not right and immediate==230:
                        assert '0xE6' in '\n'.join(original(lifter,ins,ins.operands,'<<'))
                        assert '& 31u' in '\n'.join(code)  # exact failing regression
                    index=len(forms);forms.append((width,immediate,raw))
                    blocks.append(f'case {index}: '+'{\n'+'\n'.join(code)+'\nbreak;}')
    source='''#include "recomp_types.h"
typedef struct {uint32_t gpr[8]; unsigned char memory[16]; uint32_t carry,snapshot;} State;
ptrdiff_t g_xbox_mem_offset;
'''+''.join(f'#define {r} (ctx->gpr[{i}])\n' for i,r in enumerate(('eax','ecx','edx','ebx','esp','ebp','esi','edi')))+'''
__attribute__((section(".text.entry"))) void probe(State *ctx,uint32_t id) {
int _cf=(int)ctx->carry; uint32_t _fa=ctx->snapshot,_fb=0,_zr=0; int32_t _fas=0,_fbs=0;
g_xbox_mem_offset=(ptrdiff_t)ctx->memory-0x200000;
switch(id) {
'''+ '\n'.join(blocks)+'''\n} ctx->carry=(uint32_t)_cf;ctx->snapshot=_fa;}
'''
    (out/'probe.c').write_text(source)
    (out/'layout.ld').write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    flags=['-std=gnu11','-O2','-fno-strict-aliasing','-ffreestanding','-fno-stack-protector','-nostdlib','-Werror=shift-count-overflow']
    built=subprocess.run([str(a.cc.resolve()),*flags,'-I'+str(sdk/'templates/runtime'),'-Wl,-T,'+str(out/'layout.ld'),str(out/'probe.c'),'-o',str(out/'probe.elf')],capture_output=True,text=True)
    (out/'compiler.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise RuntimeError(built.stderr)
    subprocess.run([str(a.cc.resolve().with_name('aarch64-none-elf-objcopy.exe')),'-O','binary',str(out/'probe.elf'),str(out/'probe.bin')],check=True)
    arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x100000);arm.mem_write(0x100000,(out/'probe.bin').read_bytes());arm.mem_map(0x800000,0x10000)
    x86=Uc(UC_ARCH_X86,UC_MODE_32);x86.mem_map(0x400000,0x1000);x86.mem_map(0x200000,0x1000)
    regs=[getattr(xr,'UC_X86_REG_'+r) for r in ('EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI')]
    cases=zero=defined=0
    for index,(width,immediate,raw) in enumerate(forms):
        x86.mem_write(0x400000,raw);x86.ctl_remove_cache(0x400000,0x401000)
        for count in (range(256) if immediate is None else [immediate]):
            for value in (0,1,0x7f,0x80,0xff,0x7fff,0x8000,0xffff,0x7fffffff,0x80000000,0xffffffff):
                for carry in (0,1):
                    gpr=[0x200000,count,0x12345678,value,0x808000,0xabcdef01,0x76543210,0xfedcba98]
                    memory=struct.pack('<4I',value,0x12345678,0xffffffff,0x98765432)
                    state=struct.pack('<8I',*gpr)+memory+struct.pack('<2I',carry,0x91827364)
                    for r,v in zip(regs,gpr):x86.reg_write(r,v)
                    x86.reg_write(xr.UC_X86_REG_EFLAGS,0x202|carry);x86.mem_write(0x200000,memory)
                    x86.emu_start(0x400000,0x400000+len(raw),count=1)
                    expected=struct.pack('<8I',*(x86.reg_read(r) for r in regs))+bytes(x86.mem_read(0x200000,16))
                    arm.mem_write(0x800100,state);arm.reg_write(ar.UC_ARM64_REG_X0,0x800100);arm.reg_write(ar.UC_ARM64_REG_X1,index)
                    arm.reg_write(ar.UC_ARM64_REG_SP,0x80f000);arm.reg_write(ar.UC_ARM64_REG_LR,0x1ff000)
                    arm.emu_start(0x100000,0x1ff000,count=1000)
                    actual=bytes(arm.mem_read(0x800100,56));assert actual[:48]==expected,(index,count,hex(value))
                    assert arm.reg_read(ar.UC_ARM64_REG_PC)==0x1ff000
                    if count&31<width*8 or count&31==0:
                        assert struct.unpack_from('<I',actual,48)[0]==x86.reg_read(xr.UC_X86_REG_EFLAGS)&1
                        defined+=1
                    if count&31==0:
                        assert struct.unpack_from('<I',actual,52)[0]==0x91827364;zero+=1
                    cases+=1
    assert sdk_identity(sdk)==identity and all(sha(Path(p))==h for p,h in inputs.items())
    result=dict(status='PASS',forms=len(forms),differential_cases=cases,defined_carry_cases=defined,zero_count_cases=zero,
                source_sha256=inputs,sdk_sha256=identity,compiler_flags=flags,arm64_binary_sha256=sha(out/'probe.bin'),
                limits=['No game execution','Undefined carry/overflow excluded','Incoming EFLAGS reconstruction outside this test'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','cc','output'):p.add_argument('--'+n,type=Path,required=True)
    test(p.parse_args())

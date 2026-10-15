"""Synthetic x86 vs compiled ARM64 differential tests for MUL/IMUL CF/OF.

Only architecturally defined flags are compared. No game code or hardware.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys


def forms():
    result = []
    consumers = '0f90c10f91c70f92c20f93c5'  # SETO CL; SETNO BH; SETB DL; SETAE CH
    def add(name, raw, suffix=consumers, exhaustive=False):
        result.append(dict(name=name, raw=bytes.fromhex(raw+suffix), exhaustive=exhaustive))
    for signed, extension in ((False,4),(True,5)):
        for width in (1,2,4):
            for register in range(8):
                prefix = '66' if width == 2 else ''
                opcode = 'f6' if width == 1 else 'f7'
                add(('imul' if signed else 'mul')+str(width*8),
                    prefix+opcode+f'{0xc0+extension*8+register:02x}',
                    exhaustive=(width == 1 and register == 3))  # BL independent of AL
            add('memory', ('66' if width == 2 else '')+('f6' if width == 1 else 'f7')+f'{extension*8+1:02x}')
    for width in (2,4):
        prefix = '66' if width == 2 else ''
        for modrm in ('c2','c9','d0','01'):
            add('imul-two',prefix+'0faf'+modrm)
        for modrm in ('c1','c0','09','d2'):
            for immediate in (0,1,2,127,128,255):
                add('imul-imm8',prefix+'6b'+modrm+f'{immediate:02x}')
        for value in (0,1,0x7fff,0x8000,0xffff,0x7fffffff,0x80000000,0xffffffff):
            add('imul-immediate',prefix+'69c1'+(value&((1<<(width*8))-1)).to_bytes(width,'little').hex())
    for raw in ('f7e2','f7ea','0fafc2','66f7e2','66f7ea'):
        add('overwrite-operands',raw,'b811111111ba22222222'+consumers)
        add('carry-into-adc',raw,'83d600')  # ADC ESI,0
        add('carry-into-sbb',raw,'19db')    # SBB EBX,EBX
        add('inc-preserves-carry',raw,'46'+consumers)
        add('dec-preserves-carry',raw,'4f'+consumers)
        add('seto-memory',raw,'0f90070f91c4')  # SETO [EDI]; SETNO AH
    return result


def test(sdk, deps, compiler, output):
    pc = Path(__file__).resolve().parents[1]
    sys.path[:0] = [str(sdk.resolve()), str(deps.resolve()), str(pc/'tools')]
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    from tools.recomp.disasm import Disassembler, BasicBlock
    from tools.recomp import lifter as lm
    from tools.recomp.translator import FunctionTranslator
    from aot_mul_flags import install
    from aot_simd_bits import install as install_simd
    from run_aot_analysis import sdk_identity
    from unicorn import Uc, UC_ARCH_ARM64, UC_ARCH_X86, UC_MODE_ARM, UC_MODE_32
    import unicorn.arm64_const as ar
    import unicorn.x86_const as xr
    if output.exists(): raise ValueError('Use a fresh private output directory')
    output.mkdir(parents=True)
    before_sdk = sdk_identity(sdk)
    files = [pc/'tools/aot_mul_flags.py',pc/'native/fnv_mul_flags.h',pc/'tools/aot_simd_bits.py',pc/'native/fnv_simd_bits.h']
    identities = {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    install_simd(); install(); installed = lm.Lifter.lift_instruction; install()
    assert installed is lm.Lifter.lift_instruction
    decoder = Cs(CS_ARCH_X86, CS_MODE_32); decoder.detail = True
    model = Disassembler()
    corpus = forms(); blocks = []
    preserved_sse='f30f6f07660f134710660f120f660f54c1660f55c0660f56c1660f14c1660f15c1660fc6c101660f70c1e4660f72f001660f72d001660f73f001660f73d001'
    corpus.append(dict(name='multiply-through-simd',raw=bytes.fromhex('f7e2'+preserved_sse+'0f90c10f91c70f92c2'),exhaustive=False))
    for number, form in enumerate(corpus):
        instructions = [model._decode_instruction(i) for i in decoder.disasm(form['raw'], 0x400000)]
        assert sum(i.size for i in instructions) == len(form['raw'])
        assert instructions[0].mnemonic in ('mul','imul')
        instance = lm.Lifter(); instance.needs_cf = True
        statements, state = lm.lift_basic_block(instance, BasicBlock(0x400000,instructions))
        assert not instance.unimplemented, (form['name'],instance.unimplemented)
        of = lm._make_condition('jo', *state) if state else None
        # ADC/SBB OF support is outside this overlay when the SDK cannot
        # reconstruct it. Always check CF; check OF only with a known expression.
        form['flag_mask'] = 0x801 if of is not None else 1
        flag_value = '_cf & 1u'
        if of: flag_value += f' | ((uint32_t)({of[0]} != 0) << 11)'
        blocks.append(f'case {number}u: {{ uint32_t _fnv_mul_overflow=0; int _cf=0, _flags=0;'
            'uint32_t _fa=0,_fb=0,_zr=0; int32_t _fas=0,_fbs=0;\n'+
            '\n'.join(statements)+f'\nctx->flags={flag_value}; break; }}')
        assert FunctionTranslator._function_needs_cf(instructions) or not any(i.mnemonic.startswith('setb') for i in instructions)
    unknown = model._decode_instruction(next(decoder.disasm(bytes.fromhex('0f90c1'),0x400000)))
    instance = lm.Lifter()
    lm.lift_basic_block(instance,BasicBlock(0x400000,[unknown]))
    assert 'seto' in instance.unimplemented
    assert lm._make_condition('jz','mul',[]) is None
    called = [model._decode_instruction(i) for i in decoder.disasm(bytes.fromhex('f7e2e8000000000f90c1'),0x400000)]
    instance = lm.Lifter(); instance.needs_cf = True
    lm.lift_basic_block(instance,BasicBlock(0x400000,called))
    assert 'seto' in instance.unimplemented  # no invented incoming OF across CALL
    # Preserve a known producer across a basic block and overwritten source.
    split = [model._decode_instruction(i) for i in decoder.disasm(bytes.fromhex('f7e2b8111111110f90c1'),0x400000)]
    instance = lm.Lifter(); instance.needs_cf = True
    first, state = lm.lift_basic_block(instance,BasicBlock(split[0].address,split[:1]))
    second, state = lm.lift_basic_block(instance,BasicBlock(split[1].address,split[1:]),flag_state=state)
    assert '_fnv_mul_overflow' in '\n'.join(second) and not instance.unimplemented
    source = '''#include "fnv_mul_flags.h"
#include "fnv_simd_bits.h"
typedef struct {uint32_t gpr[8]; unsigned char memory[128]; uint32_t flags, diagnostic;} State;
_Static_assert(offsetof(State,memory)==32 && sizeof(State)==168,"state layout");
ptrdiff_t g_xbox_mem_offset;
'''+''.join(f'#define {r} (ctx->gpr[{i}])\n' for i,r in enumerate(('eax','ecx','edx','ebx','esp','ebp','esi','edi')))+'''
__attribute__((section(".text.entry"))) void probe(State *ctx,uint32_t id) {
RecompXmm xmm0=XMM_ZERO(),xmm1=XMM_ZERO();
g_xbox_mem_offset=(ptrdiff_t)ctx->memory-0x200000;
switch(id) {
'''+ '\n'.join(blocks)+'''\ndefault:ctx->diagnostic=1;break;}}
'''
    (output/'probe.c').write_text(source)
    layout = output/'layout.ld'
    layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    flags = ['-std=gnu11','-O2','-fno-strict-aliasing','-ffp-contract=off','-mearly-ra=none','-ffreestanding','-fno-stack-protector','-nostdlib','-Werror=implicit-function-declaration']
    command = [str(compiler.resolve()),*flags,'-I',str(pc/'native'),'-I',str(sdk.resolve()/'templates/runtime'),
               '-Wl,-T,'+str(layout.resolve()),str((output/'probe.c').resolve()),'-o',str((output/'probe.elf').resolve())]
    built = subprocess.run(command,capture_output=True,text=True)
    (output/'compiler.log').write_text(built.stdout+built.stderr)
    if built.returncode: raise RuntimeError(built.stderr)
    subprocess.run([str(compiler.with_name('aarch64-none-elf-objcopy.exe').resolve()),'-O','binary',str(output/'probe.elf'),str(output/'probe.bin')],check=True)
    arm = Uc(UC_ARCH_ARM64,UC_MODE_ARM); arm.mem_map(0x100000,0x100000)
    binary = (output/'probe.bin').read_bytes(); arm.mem_write(0x100000,binary)
    arm.mem_map(0x800000,0x10000)
    x86 = Uc(UC_ARCH_X86,UC_MODE_32); x86.mem_map(0x400000,0x1000); x86.mem_map(0x200000,0x1000)
    regs = [getattr(xr,'UC_X86_REG_'+r) for r in ('EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI')]
    rng = random.Random(0xF0CF0F)
    boundary = [0,1,2,127,128,255,256,32767,32768,65535,65536,0x7fffffff,0x80000000,0xffffffff]
    cases = exhaustive_cases = cf_of_cases = cf_only_cases = 0
    for number, form in enumerate(corpus):
        x86.mem_write(0x400000,form['raw']); x86.ctl_remove_cache(0x400000,0x401000)
        vectors = ((a,b) for a in range(256) for b in range(256)) if form['exhaustive'] else (
            [(a,b) for a in boundary for b in boundary]+[(rng.getrandbits(32),rng.getrandbits(32)) for _ in range(96)])
        for left,right in vectors:
            gpr = [left]*8
            gpr[2]=right; gpr[3]=right; gpr[1]=0x200000+(cases%4); gpr[7]=0x200060
            state = bytearray(struct.pack('<8I',*gpr)+rng.randbytes(128)+struct.pack('<II',0,0))
            # Fill a memory operand with the same independently varied source.
            struct.pack_into('<I',state,32+(cases%4),right)
            initial_flags = 0x202 | (rng.getrandbits(12)&0x8d5)
            x86.reg_write(xr.UC_X86_REG_EFLAGS,initial_flags)
            for register,value in zip(regs,gpr): x86.reg_write(register,value)
            x86.reg_write(xr.UC_X86_REG_XMM0,0);x86.reg_write(xr.UC_X86_REG_XMM1,0)
            x86.mem_write(0x200000,bytes(state[32:160]))
            x86.emu_start(0x400000,0x400000+len(form['raw']),count=32)
            expected = bytearray(state)
            struct.pack_into('<8I',expected,0,*(x86.reg_read(r) for r in regs))
            expected[32:160]=bytes(x86.mem_read(0x200000,128))
            struct.pack_into('<I',expected,160,x86.reg_read(xr.UC_X86_REG_EFLAGS)&form['flag_mask'])
            arm.mem_write(0x800100,bytes(state)); arm.reg_write(ar.UC_ARM64_REG_X0,0x800100)
            arm.reg_write(ar.UC_ARM64_REG_X1,number); arm.reg_write(ar.UC_ARM64_REG_SP,0x80f000)
            arm.reg_write(ar.UC_ARM64_REG_LR,0x1ff000)
            arm.emu_start(0x100000,0x1ff000,count=5000)
            actual = bytearray(arm.mem_read(0x800100,168))
            struct.pack_into('<I',actual,160,struct.unpack_from('<I',actual,160)[0]&form['flag_mask'])
            assert actual == expected,(number,form['name'],hex(left),hex(right),actual.hex(),expected.hex())
            assert arm.reg_read(ar.UC_ARM64_REG_PC)==0x1ff000
            cases += 1; exhaustive_cases += int(form['exhaustive'])
            cf_of_cases += int(form['flag_mask'] == 0x801)
            cf_only_cases += int(form['flag_mask'] == 1)
    assert sdk_identity(sdk)==before_sdk
    assert identities=={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
    result = dict(status='PASS',forms=len(corpus),differential_cases=cases,exhaustive_byte_pairs=exhaustive_cases,
        cf_and_of_cases=cf_of_cases,cf_only_cases=cf_only_cases,
        sdk_sha256=before_sdk,source_sha256=identities,compiler_flags=flags,
        arm64_binary_sha256=hashlib.sha256(binary).hexdigest(),
        limits=['Undefined multiply flags not compared','ADC/SBB sequences compare CF only when the SDK has no OF expression',
                'No generic incoming OF support','No engine execution'],native_engine_started=False)
    (output/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,required=True); p.add_argument('--python-deps',type=Path,required=True)
    p.add_argument('--cc',type=Path,required=True); p.add_argument('--output',type=Path,required=True)
    a=p.parse_args(); test(a.sdk,a.python_deps,a.cc,a.output)

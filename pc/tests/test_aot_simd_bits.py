"""Compare actual overlay C compiled to ARM64 with x86 execution in Unicorn.

Synthetic instructions/data only. This verifies mapped-memory semantics and
fail-stop alignment guards, not guest exception delivery or engine execution.
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
    def add(name, raw, control=None):
        result.append(dict(name=name, raw=bytes.fromhex(raw), control=control))
    for opcode, name in [('54','andpd'), ('55','andnpd'), ('56','orpd'),
                         ('14','unpcklpd'), ('15','unpckhpd')]:
        for modrm in ('c1', 'db', 'f8', '01', '39'):
            add(name, '660f'+opcode+modrm)
    for name, prefix, opcode, stores in [('movlpd','66','12','13'), ('movdqu','f3','6f','7f')]:
        for modrm in ('01','19','39'):
            add(name+'-load', prefix+'0f'+opcode+modrm)
            add(name+'-store', prefix+'0f'+stores+modrm)
        if name == 'movdqu':
            for modrm in ('c1', 'db', 'f8'): add(name+'-reg', prefix+'0f'+opcode+modrm)
    for name, opcode in [('pshufd','70'), ('shufpd','c6')]:
        for immediate in range(256):
            for modrm in ('c1', 'db'):
                add(name, '660f'+opcode+modrm+f'{immediate:02x}')
        for modrm in ('01','39','f8'):
            for immediate in (0,1,2,3,0xe4,0xff):
                add(name, '660f'+opcode+modrm+f'{immediate:02x}')
    for name, opcode, modrm, variable in [('pslld','72','f3','f2'), ('psrld','72','d3','d2'),
                                         ('psllq','73','f3','f3'), ('psrlq','73','d3','d3')]:
        for immediate in range(256):
            add(name+'-immediate', '660f'+opcode+modrm+f'{immediate:02x}')
        for operand in ('c1','db','01','39','f8'):
            for count in (0,1,31,32,33,63,64,65,255,256,0x100000000,0x8000000000000000,0xffffffffffffffff):
                add(name+'-variable', '660f'+variable+operand, count)
    return result


def test(sdk, deps, compiler, output):
    pc = Path(__file__).resolve().parents[1]
    sys.path[:0] = [str(deps.resolve()), str(sdk.resolve()), str(pc/'tools')]
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    from tools.recomp.disasm import Disassembler, Operand
    from tools.recomp.lifter import Lifter
    from aot_simd_bits import emit, install, SUPPORTED
    from run_aot_analysis import sdk_identity
    from unicorn import Uc, UcError, UC_ARCH_ARM64, UC_ARCH_X86, UC_MODE_ARM, UC_MODE_32
    import unicorn.arm64_const as ar
    import unicorn.x86_const as xr
    if output.exists(): raise ValueError('Use a new private test output directory')
    output.mkdir(parents=True)
    sdk_before = sdk_identity(sdk)
    source_files = [pc/'tools/aot_simd_bits.py', pc/'native/fnv_simd_bits.h']
    identities = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files}
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    model = Disassembler()
    corpus = forms()
    blocks = []
    observed = set()
    for number, form in enumerate(corpus):
        decoded = list(decoder.disasm(form['raw'], 0x400000))
        assert len(decoded) == 1 and decoded[0].size == len(form['raw'])
        instruction = model._decode_instruction(decoded[0])
        emitted = emit(instruction)
        assert emitted is not None, form
        observed.add(instruction.mnemonic)
        blocks.append(f'case {number}u: '+' '.join(emitted)+' break;')
        form['instruction'] = instruction
    assert observed == SUPPORTED
    # Reject malformed forms and leave unrelated SDK translation unchanged.
    from dataclasses import replace
    sample = corpus[0]['instruction']
    assert emit(replace(sample, mnemonic='movdqa')) is None
    assert emit(replace(sample, operands=[Operand(type='reg', reg='mm0'), sample.operands[1]])) is None
    assert emit(replace(sample, mnemonic='movlpd')) is None  # invalid register-register form
    assert emit(replace(sample, operands=[sample.operands[0], Operand(type='mem', mem_size=8)])) is None
    nop = model._decode_instruction(next(decoder.disasm(b'\x90', 0x400000)))
    stock = Lifter().lift_instruction(nop)
    install(); patched = Lifter.lift_instruction; install()
    assert Lifter.lift_instruction is patched and Lifter().lift_instruction(nop) == stock
    assert Lifter().lift_instruction(sample) == emit(sample)
    # Every newly supported form must preserve the producer/operands of the
    # incoming integer flags; otherwise a later Jcc/SETcc silently loses them.
    from tools.recomp.lifter import lift_basic_block
    from tools.recomp.disasm import BasicBlock
    for form in corpus:
        statement,state=lift_basic_block(Lifter(),BasicBlock(0x400000,[form['instruction']]),
            flag_state=('test',[Operand(type='reg',reg='eax'),Operand(type='reg',reg='eax')]))
        assert state and state[0] == 'test',form['name']
    source = '''#include "fnv_simd_bits.h"
typedef struct { uint32_t gpr[8]; RecompXmm xmm[8]; unsigned char memory[96];
  uint32_t eflags, mxcsr, diagnostic; } State;
_Static_assert(offsetof(State, memory)==160 && sizeof(State)==272, "State layout");
ptrdiff_t g_xbox_mem_offset;
static uint32_t *diagnostic;
void recomp_unimpl(const char *text, uint32_t va) { (void)text; *diagnostic=va; }
void abort(void) { __builtin_trap(); }
#define eax (ctx->gpr[0])
#define ecx (ctx->gpr[1])
#define edx (ctx->gpr[2])
#define ebx (ctx->gpr[3])
#define esp (ctx->gpr[4])
#define ebp (ctx->gpr[5])
#define esi (ctx->gpr[6])
#define edi (ctx->gpr[7])
'''+''.join(f'#define xmm{i} (ctx->xmm[{i}])\n' for i in range(8))+'''
__attribute__((section(".text.entry"))) void probe(State *ctx, uint32_t id) {
 g_xbox_mem_offset=(ptrdiff_t)ctx->memory-0x200000;
 diagnostic=&ctx->diagnostic;
 switch(id) {
'''+ '\n'.join(blocks)+'''\n default: *diagnostic=0xffffffffu; break; }
}
'''
    (output/'probe.c').write_text(source)
    layout = output/'layout.ld'
    layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    flags = ['-std=gnu11','-O2','-fno-strict-aliasing','-ffp-contract=off','-mearly-ra=none',
             '-ffreestanding','-fno-stack-protector','-nostdlib']
    command = [str(compiler.resolve()), *flags, '-I', str(pc/'native'),
        '-I', str(sdk.resolve()/'templates/runtime'), '-Wl,-T,'+str(layout.resolve()),
        str((output/'probe.c').resolve()), '-o', str((output/'probe.elf').resolve())]
    compiled = subprocess.run(command, capture_output=True, text=True)
    (output/'compiler.log').write_text(compiled.stdout+compiled.stderr)
    if compiled.returncode: raise RuntimeError(compiled.stderr)
    subprocess.run([str(compiler.with_name('aarch64-none-elf-objcopy.exe').resolve()), '-O','binary',
                    str(output/'probe.elf'), str(output/'probe.bin')], check=True)
    code = (output/'probe.bin').read_bytes()
    assert len(code) < 0x3f0000
    arm = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    arm.mem_map(0x100000, 0x400000); arm.mem_write(0x100000, code)
    arm.mem_map(0x800000, 0x10000)
    x86 = Uc(UC_ARCH_X86, UC_MODE_32)
    x86.mem_map(0x400000, 0x1000); x86.mem_map(0x200000, 0x1000)
    xregs = [getattr(xr, 'UC_X86_REG_'+name) for name in ('EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI')]
    xmms = [getattr(xr,'UC_X86_REG_XMM'+str(i)) for i in range(8)]
    rng = random.Random(0xF0A751)
    patterns = [bytes(16), bytes([255])*16, bytes(range(16)),
                bytes.fromhex('010000000000f07f452301000000f87f'), rng.randbytes(16)]
    cases = faults = 0
    for index, form in enumerate(corpus):
        x86.mem_write(0x400000, form['raw']); x86.ctl_remove_cache(0x400000, 0x401000)
        for pattern in patterns:
            state = bytearray(rng.randbytes(272))
            regs = [rng.getrandbits(32) for _ in range(8)]
            # MOVLPD/MOVDQU exercise all byte alignments; other legacy memory
            # forms use aligned m128 and have a separate fail-stop test below.
            unaligned = form['name'].startswith(('movlpd','movdqu'))
            offset = cases % 16 if unaligned else 16
            regs[1] = 0x200000+offset
            struct.pack_into('<8I', state, 0, *regs)
            state[32:160] = b''.join(pattern if i%2 == 0 else rng.randbytes(16) for i in range(8))
            control = form['control']
            if control is not None:
                operand = form['instruction'].operands[1]
                at = 32+int(operand.reg[3:])*16 if operand.type == 'reg' else 160+offset
                # High qword must be ignored, even when it contains all ones.
                struct.pack_into('<QQ', state, at, control, 0xffffffffffffffff)
            initial_flags = 0x202 | (rng.getrandbits(12)&0x8d5)
            initial_mxcsr = 0x1f80 | rng.randrange(64) | rng.randrange(4)<<13
            struct.pack_into('<III', state, 256, initial_flags, initial_mxcsr, 0)
            for reg, value in zip(xregs, regs): x86.reg_write(reg, value)
            for i, reg in enumerate(xmms): x86.reg_write(reg, int.from_bytes(state[32+i*16:48+i*16], 'little'))
            x86.reg_write(xr.UC_X86_REG_EFLAGS, initial_flags)
            x86.reg_write(xr.UC_X86_REG_MXCSR, initial_mxcsr)
            x86.mem_write(0x200000, bytes(state[160:256]))
            x86.emu_start(0x400000, 0x400000+len(form['raw']), count=1)
            expected = bytearray(state)
            struct.pack_into('<8I', expected, 0, *(x86.reg_read(reg) for reg in xregs))
            expected[32:160] = b''.join(x86.reg_read(reg).to_bytes(16,'little') for reg in xmms)
            expected[160:256] = bytes(x86.mem_read(0x200000,96))
            struct.pack_into('<II', expected, 256, x86.reg_read(xr.UC_X86_REG_EFLAGS), x86.reg_read(xr.UC_X86_REG_MXCSR))
            arm.mem_write(0x800100, bytes(state))
            arm.reg_write(ar.UC_ARM64_REG_X0, 0x800100); arm.reg_write(ar.UC_ARM64_REG_X1, index)
            arm.reg_write(ar.UC_ARM64_REG_SP, 0x80f000); arm.reg_write(ar.UC_ARM64_REG_LR, 0x4ff000)
            arm.emu_start(0x100000, 0x4ff000, count=10000)
            actual = bytes(arm.mem_read(0x800100,272))
            assert actual == bytes(expected), (index, form['name'], form['raw'].hex(), control)
            assert arm.reg_read(ar.UC_ARM64_REG_PC) == 0x4ff000
            cases += 1
        # Invalid legacy m128 must never reach a successful operation. This is
        # intentionally a diagnostic + abort, not a claimed emulated #GP/SEH.
        if not form['name'].startswith(('movlpd','movdqu')) and \
                any(op.type == 'mem' for op in form['instruction'].operands):
            struct.pack_into('<I', state, 4, 0x200001)
            arm.mem_write(0x800100, bytes(state))
            arm.reg_write(ar.UC_ARM64_REG_X0, 0x800100); arm.reg_write(ar.UC_ARM64_REG_X1, index)
            arm.reg_write(ar.UC_ARM64_REG_SP, 0x80f000); arm.reg_write(ar.UC_ARM64_REG_LR, 0x4ff000)
            try:
                arm.emu_start(0x100000, 0x4ff000, count=10000)
            except UcError:
                pass
            else:
                raise AssertionError('Misaligned legacy operand did not fail-stop')
            expected = bytearray(state); struct.pack_into('<I',expected,264,0x400000)
            assert bytes(arm.mem_read(0x800100,272)) == bytes(expected)
            faults += 1
    assert sdk_identity(sdk) == sdk_before
    assert identities == {p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files}
    result = dict(status='PASS', instruction_families=sorted(observed), forms=len(corpus),
        differential_cases=cases, fail_stop_alignment_cases=faults, compiler_flags=flags,
        arm64_binary_sha256=hashlib.sha256(code).hexdigest(), sdk_sha256=sdk_before,
        source_sha256=identities,
        all_shuffle_and_immediate_shift_controls_tested=True,
        guest_exception_delivery_verified=False, native_engine_started=False)
    (output/'validation.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--python-deps', type=Path, required=True)
    parser.add_argument('--cc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    test(args.sdk, args.python_deps, args.cc, args.output)

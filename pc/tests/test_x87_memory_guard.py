"""Execute ARM64 stop paths for decoded, unsupported ten-byte x87 operands.

This verifies fail-closed code generation, not extended precision arithmetic.
No game binary or captured input is required.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys


def test(a):
    pc = Path(__file__).resolve().parents[1]
    sys.path[:0] = [str(a.sdk.resolve()), str(a.python_deps.resolve()), str(pc/'tools')]
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    from tools.recomp.disasm import Disassembler
    from tools.recomp.lifter import Lifter
    from aot_frame_restore import install
    from run_aot_analysis import sdk_identity, sha
    from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
    import unicorn.arm64_const as ar
    if a.output.exists():
        raise ValueError('Fresh output required')
    a.output.mkdir(parents=True)
    out = a.output.resolve()
    identity = sdk_identity(a.sdk.resolve())
    inputs = {str(p.resolve()): sha(p) for p in
              (Path(__file__), pc/'tools/aot_frame_restore.py', a.cc)}
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    disassembler = Disassembler()
    install()
    installed = Lifter.lift_instruction
    install()
    assert installed is Lifter.lift_instruction
    blocks = []
    cases = []
    for opcode, absolute, stack, mnemonic in (
            (0xdb, 0x2d, 0x6d, 'fld'), (0xdb, 0x3d, 0x7d, 'fstp'),
            (0xdf, 0x25, 0x65, 'fbld'), (0xdf, 0x35, 0x75, 'fbstp')):
        for modrm, displacement in ((absolute, b'\x00\x00\x20\x00'), (stack, b'\xf0')):
            raw = bytes([opcode, modrm]) + displacement
            address = 0x400000 + len(cases)*0x10
            decoded = list(decoder.disasm(raw, address))
            assert len(decoded) == 1 and decoded[0].size == len(raw)
            ins = disassembler._decode_instruction(decoded[0])
            assert ins.mnemonic == mnemonic and ins.operands[0].mem_size == 10
            lifter = Lifter()
            code = lifter.lift_instruction(ins)
            assert lifter.unimplemented == {mnemonic: [address]}
            assert len(code) == 1 and 'RECOMP_UNIMPL(' in code[0]
            blocks.append(f'case {len(cases)}: ' + code[0] + '\nbreak;')
            cases.append(dict(address=address, mnemonic=mnemonic, bytes=raw.hex()))
    # Ordinary float, double and register forms must stay implemented.
    for raw in (b'\xd9\x00', b'\xdd\x00', b'\xd9\x18', b'\xdd\x18', b'\xdd\xd8'):
        ins = disassembler._decode_instruction(next(decoder.disasm(raw, 0x500000)))
        lifter = Lifter()
        assert 'RECOMP_UNIMPL' not in '\n'.join(lifter.lift_instruction(ins))
        assert not lifter.unimplemented
    source = '''#include <stdint.h>
typedef struct {uint32_t address,count; unsigned char canary[64];} State;
#define RECOMP_UNIMPL(text,va) do {ctx->address=(va);ctx->count++;return;} while(0)
__attribute__((section(".text.entry"))) void probe(State *ctx,uint32_t id) {
switch(id) {
''' + '\n'.join(blocks) + '\n}\n}\n'
    (out/'probe.c').write_text(source)
    (out/'layout.ld').write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    flags = ['-std=gnu11', '-O2', '-ffreestanding', '-fno-stack-protector', '-nostdlib', '-Wall', '-Wextra', '-Werror']
    command = [str(a.cc.resolve()), *flags, '-Wl,-T,'+str(out/'layout.ld'), str(out/'probe.c'), '-o', str(out/'probe.elf')]
    built = subprocess.run(command, capture_output=True, text=True)
    (out/'compiler.log').write_text(built.stdout+built.stderr)
    if built.returncode:
        raise RuntimeError(built.stderr)
    subprocess.run([str(a.cc.resolve().with_name('aarch64-none-elf-objcopy.exe')), '-O', 'binary', str(out/'probe.elf'), str(out/'probe.bin')], check=True)
    arm = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    arm.mem_map(0x100000, 0x100000)
    arm.mem_write(0x100000, (out/'probe.bin').read_bytes())
    arm.mem_map(0x800000, 0x10000)
    for index, case in enumerate(cases):
        canary = bytes(range(64))
        arm.mem_write(0x800100, struct.pack('<2I', 0, 0)+canary)
        arm.reg_write(ar.UC_ARM64_REG_X0, 0x800100)
        arm.reg_write(ar.UC_ARM64_REG_X1, index)
        arm.reg_write(ar.UC_ARM64_REG_SP, 0x80f000)
        arm.reg_write(ar.UC_ARM64_REG_LR, 0x1ff000)
        arm.emu_start(0x100000, 0x1ff000, count=1000)
        assert arm.reg_read(ar.UC_ARM64_REG_PC) == 0x1ff000
        assert bytes(arm.mem_read(0x800100, 72)) == struct.pack('<2I', case['address'], 1)+canary
    assert sdk_identity(a.sdk.resolve()) == identity
    assert all(sha(Path(p)) == h for p, h in inputs.items())
    result = dict(status='PASS', arm64_stop_cases=len(cases), supported_form_checks=5,
                  cases=cases, source_sha256=inputs, sdk_sha256=identity,
                  compiler_flags=flags, arm64_binary_sha256=sha(out/'probe.bin'),
                  limits=['No x87 extended arithmetic implementation', 'No game execution', 'No Switch hardware execution'])
    (out/'validation.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'python-deps', 'cc', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    test(parser.parse_args())

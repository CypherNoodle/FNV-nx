"""Explicit SSE2 codegen overlay; never edits the supplied SDK sources.

Only listed legacy x86-32 forms are added. Everything else falls back to the
SDK, including aligned MOVDQA and all floating arithmetic/control-state work.
Generated files must include fnv_simd_bits.h from this project's native folder.
"""
import argparse
import runpy
import sys
from pathlib import Path

SUPPORTED = frozenset(('movlpd', 'movdqu', 'andpd', 'andnpd', 'orpd',
    'pshufd', 'shufpd', 'unpcklpd', 'unpckhpd', 'pslld', 'psrld', 'psllq', 'psrlq'))


def emit(insn):
    from tools.recomp.lifter import _fmt_mem
    m, ops = insn.mnemonic, insn.operands
    def xmm(op):
        return op.type == 'reg' and op.reg in {'xmm'+str(i) for i in range(8)}
    def memory(op, size):
        return op.type == 'mem' and op.mem_size == size
    def packed(op):
        if xmm(op): return op.reg
        if memory(op, 16): return f'fnv_xmm_aligned_mem({_fmt_mem(op)}, 0x{insn.address:08X}u)'
        return None
    def imm8(op):
        return op.type == 'imm' and 0 <= op.imm <= 255
    if m not in SUPPORTED or len(ops) not in (2, 3): return None
    dst, source = ops[:2]
    if m == 'movlpd' and len(ops) == 2:
        if xmm(dst) and memory(source, 8):
            return [f'{dst.reg} = fnv_xmm_loadlpd({dst.reg}, {_fmt_mem(source)}); /* movlpd */']
        if memory(dst, 8) and xmm(source):
            return [f'fnv_xmm_storelpd({_fmt_mem(dst)}, {source.reg}); /* movlpd */']
        return None
    if m == 'movdqu' and len(ops) == 2:
        value = source.reg if xmm(source) else f'fnv_xmm_loadu({_fmt_mem(source)})' if memory(source, 16) else None
        if value is not None and xmm(dst):
            return [f'{dst.reg} = {value}; /* movdqu */']
        if memory(dst, 16) and xmm(source):
            return [f'fnv_xmm_storeu({_fmt_mem(dst)}, {source.reg}); /* movdqu */']
        return None
    if not xmm(dst): return None
    binary = {'andpd': 'XMM_AND', 'andnpd': 'XMM_ANDN', 'orpd': 'XMM_OR',
              'unpcklpd': 'fnv_xmm_unpcklpd', 'unpckhpd': 'fnv_xmm_unpckhpd'}
    value = packed(source)
    if m in binary and len(ops) == 2 and value is not None:
        return [f'{dst.reg} = {binary[m]}({dst.reg}, {value}); /* {m} */']
    if m in ('pshufd', 'shufpd') and len(ops) == 3 and value is not None and imm8(ops[2]):
        inputs = f'{dst.reg}, {value}' if m == 'shufpd' else value
        return [f'{dst.reg} = fnv_xmm_{m}({inputs}, {ops[2].imm}u); /* {m} */']
    if m in ('pslld', 'psrld', 'psllq', 'psrlq') and len(ops) == 2:
        count = f'{source.imm}u' if imm8(source) else f'({value}).q[0]' if value is not None else None
        if count is not None:
            return [f'{dst.reg} = fnv_xmm_{m}({dst.reg}, {count}); /* {m} */']
    return None


def install():
    from tools.recomp import lifter as module
    from tools.recomp.lifter import Lifter
    if getattr(Lifter.lift_instruction, '_fnv_simd_bits', False): return
    module._EFLAGS_PRESERVE = module._EFLAGS_PRESERVE | SUPPORTED
    original = Lifter.lift_instruction
    def lift(self, insn):
        result = emit(insn)
        return original(self, insn) if result is None else result
    lift._fnv_simd_bits = True
    Lifter.lift_instruction = lift


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    args, rest = parser.parse_known_args()
    sys.path.insert(0, str(args.sdk.resolve()))
    install()
    sys.argv = ['tools.recomp', *rest]
    runpy.run_module('tools.recomp', run_name='__main__')

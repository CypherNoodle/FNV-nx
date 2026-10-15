"""MUL/IMUL data and defined CF/OF for x86-32, as an explicit SDK overlay.

No generic SETO fallback: only a known multiplying producer supplies OF.
Unknown incoming flags, undefined multiply flags and other producers remain
subject to the SDK's existing limitations. SDK sources are never changed.
"""
import argparse
from pathlib import Path
import re
import runpy
import sys


def emit(insn, carry=False):
    from tools.recomp.lifter import _operand_width, _fmt_operand_read, _fmt_operand_write, _fmt_mem
    m, ops = insn.mnemonic, insn.operands
    if m not in ('mul', 'imul') or not 1 <= len(ops) <= 3:
        return None
    if m == 'mul' and len(ops) != 1:
        return None
    width = _operand_width(ops[0])
    if width not in (1, 2, 4) or (len(ops) > 1 and width == 1):
        return None
    gprs = {'al','ah','bl','bh','cl','ch','dl','dh','ax','bx','cx','dx',
            'si','di','bp','sp','eax','ebx','ecx','edx','esi','edi','ebp','esp'}
    def operand(op):
        if op.type == 'reg' and op.reg in gprs and _operand_width(op) == width:
            return _fmt_operand_read(op)
        if op.type == 'mem' and op.mem_size == width:
            return f'fnv_mul_memory({_fmt_mem(op)}, {width}u)'
        return None
    if len(ops) == 1:
        left, right = 'eax', operand(ops[0])
    else:
        if ops[0].type != 'reg' or ops[0].reg not in gprs:
            return None
        left = operand(ops[0] if len(ops) == 2 else ops[1])
        right = operand(ops[1]) if len(ops) == 2 else (
            f'0x{ops[2].imm & 0xffffffff:08X}u' if ops[2].type == 'imm' else None)
    if left is None or right is None:
        return None
    bits = width*8
    mask = (1 << bits)-1
    lines = [f'{{ uint32_t _fnv_mul_a = (uint32_t)({left}) & 0x{mask:X}u;',
             f'  uint32_t _fnv_mul_b = (uint32_t)({right}) & 0x{mask:X}u;']
    if m == 'mul':
        lines += ['  uint64_t _fnv_mul_product = (uint64_t)_fnv_mul_a * (uint64_t)_fnv_mul_b;',
                  f'  _fnv_mul_overflow = (_fnv_mul_product >> {bits}) != 0;']
    else:
        lines += [f'  int64_t _fnv_mul_signed_product = fnv_mul_signed(_fnv_mul_a, {bits}u) * fnv_mul_signed(_fnv_mul_b, {bits}u);',
                  '  uint64_t _fnv_mul_product = (uint64_t)_fnv_mul_signed_product;',
                  f'  _fnv_mul_overflow = (_fnv_mul_signed_product < -INT64_C({1 << (bits-1)}) || _fnv_mul_signed_product > INT64_C({(1 << (bits-1))-1}));']
    if carry:
        lines.append('  _cf = (int)_fnv_mul_overflow;')
    if len(ops) > 1:
        lines.append('  '+_fmt_operand_write(ops[0], '(uint32_t)_fnv_mul_product'))
    elif width == 1:
        lines.append('  eax = (eax & 0xFFFF0000u) | (uint32_t)(_fnv_mul_product & 0xFFFFu);')
    elif width == 2:
        lines += ['  eax = (eax & 0xFFFF0000u) | (uint32_t)(_fnv_mul_product & 0xFFFFu);',
                  '  edx = (edx & 0xFFFF0000u) | (uint32_t)((_fnv_mul_product >> 16) & 0xFFFFu);']
    else:
        lines += ['  eax = (uint32_t)_fnv_mul_product;', '  edx = (uint32_t)(_fnv_mul_product >> 32);']
    lines.append(f'}} /* fnv {m}: product and CF/OF */')
    return lines


def install():
    from tools.recomp import lifter as module
    from tools.recomp.translator import FunctionTranslator
    if getattr(module.Lifter.lift_instruction, '_fnv_mul_flags', False):
        return
    original_lift = module.Lifter.lift_instruction
    original_condition = module._make_condition
    original_translate = FunctionTranslator.translate_function
    def lift(self, instruction):
        emitted = emit(instruction, self.needs_cf)
        return original_lift(self, instruction) if emitted is None else emitted
    def condition(jcc, setter, operands):
        if setter in ('mul', 'imul'):
            if jcc in ('jo','jb','jc','jnae'):
                return '(_fnv_mul_overflow != 0)', 'multiply CF/OF'
            if jcc in ('jno','jae','jnb','jnc'):
                return '(_fnv_mul_overflow == 0)', 'multiply CF/OF clear'
            return None  # ZF/SF/PF/AF are architecturally undefined here.
        return original_condition(jcc, setter, operands)
    def translate(self, address, info):
        code = original_translate(self, address, info)
        if code and '_fnv_mul_overflow' in code:
            code, count = re.subn(r'(void \w+\(void\)\s*\{\n)',
                r'\1    uint32_t _fnv_mul_overflow = 0; /* defined multiply flags */\n', code, count=1)
            if count != 1:
                raise ValueError('Unexpected generated function layout for multiply flags')
        return code
    lift._fnv_mul_flags = True
    module.Lifter.lift_instruction = lift
    module._make_condition = condition
    # MUL's ZF/SF/PF/AF are undefined, but its CF/OF are defined. Track those
    # before the SDK's generic "undefined flags" case clears all knowledge.
    module.FLAG_SETTERS = module.FLAG_SETTERS | {'mul'}
    module.CF_TRACKED = module.CF_TRACKED | {'mul','imul'}
    # CALL itself preserves flags, but its callee may change them. This SDK
    # has function-local flag snapshots, not a shared architectural EFLAGS
    # register. Do not propagate a pre-call snapshot through an unknown callee.
    module._FLAGS_UNDEFINED = module._FLAGS_UNDEFINED | {'call'}
    FunctionTranslator.translate_function = translate


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--simd-bits', action='store_true')
    args, rest = parser.parse_known_args()
    sys.path.insert(0, str(args.sdk.resolve()))
    if args.simd_bits:
        from aot_simd_bits import install as install_simd
        install_simd()
    install()
    sys.argv = ['tools.recomp', *rest]
    runpy.run_module('tools.recomp', run_name='__main__')

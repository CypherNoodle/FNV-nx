"""Explicit EBP frame-publication overlay; no SDK source edits.

The SDK publishes frame establishment, but these restores left its frame
shadows pointing at a dead inner frame. LEA-based frame establishment also
needs publication. Exact F3 C3 REP RET is a near return, not an unsupported
string operation; mark it as a terminator and lift it through the normal RET.
Normal nested registration helpers
then unwound using that stale frame. This is not exception dispatch or SEH.

The same mandatory baseline overlay also corrects SHL/SHR counts: x86-32
masks them to five bits, including byte/word operands. C must not shift by
an unmasked count or left-shift a promoted signed byte. A zero count keeps
the destination and the tracked flags unchanged. Undefined CF for counts
at or beyond the narrow operand width is not an x86 compatibility claim.
"""
import argparse
from dataclasses import replace
from pathlib import Path
import runpy
import sys


def install():
    from tools.recomp.lifter import Lifter
    from tools.recomp.lifter import _fmt_operand_read, _fmt_operand_write, _operand_width
    from tools.recomp.disasm import Instruction
    if getattr(Lifter.lift_instruction, '_fnv_frame_restore', False):
        return
    original = Lifter.lift_instruction
    def logical_shift(self, instruction, operands, operator):
        if len(operands) != 2:
            return ['/* shift: bad operands */']
        width = (_operand_width(operands[0]) or 4) * 8
        destination = _fmt_operand_read(operands[0])
        count = _fmt_operand_read(operands[1])
        out = [f'{{ uint32_t _fnv_shift_count = (uint32_t)({count}) & 31u;',
               f'  uint32_t _fnv_shift_value = (uint32_t)({destination});',
               '  if (_fnv_shift_count) {']
        if self.needs_cf:
            bit = '_fnv_shift_count - 1u' if operator == '>>' else f'{width}u - _fnv_shift_count'
            out.append(f'    if (_fnv_shift_count < {width}u) _cf = (int)((_fnv_shift_value >> ({bit})) & 1u);')
            out.append('    else _cf = 0; /* CF undefined at/above operand width */')
        out += ['    '+_fmt_operand_write(operands[0], f'_fnv_shift_value {operator} _fnv_shift_count'),
                '    '+self._result_snapshot(operands, 'shift'), '  }', '}']
        return out
    Lifter._lift_shift = logical_shift
    original_ret = Instruction.is_ret.fget
    def prefixed_return(instruction):
        return instruction.size == 2 and instruction.bytes_hex.replace(' ', '').lower() == 'f3c3' and not instruction.operands
    Instruction.is_ret = property(lambda instruction: prefixed_return(instruction) or original_ret(instruction))
    def lift(self, instruction):
        if prefixed_return(instruction):
            instruction = replace(instruction, mnemonic='ret', op_str='')
        result = original(self, instruction)
        operands = instruction.operands
        restored = instruction.mnemonic == 'leave' or (
            instruction.mnemonic == 'pop' and len(operands) == 1 and
            operands[0].type == 'reg' and operands[0].reg == 'ebp') or (
            instruction.mnemonic == 'lea' and len(operands) == 2 and
            operands[0].type == 'reg' and operands[0].reg == 'ebp' and operands[1].type == 'mem')
        if restored:
            result = [*result, 'g_ebp = ebp; g_seh_ebp = ebp; /* restored frame */']
        return result
    lift._fnv_frame_restore = True
    Lifter.lift_instruction = lift


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--simd-bits', action='store_true')
    parser.add_argument('--mul-flags', action='store_true')
    a, remaining = parser.parse_known_args()
    sys.path.insert(0, str(a.sdk.resolve()))
    if a.simd_bits:
        from aot_simd_bits import install as simd
        simd()
    if a.mul_flags:
        from aot_mul_flags import install as multiply
        multiply()
    install()
    sys.argv = ['tools.recomp', *remaining]
    runpy.run_module('tools.recomp', run_name='__main__')

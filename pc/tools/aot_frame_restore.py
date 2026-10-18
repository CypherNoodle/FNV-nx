"""Explicit EBP frame-publication overlay; no SDK source edits.

The SDK publishes frame establishment, but these restores left its frame
shadows pointing at a dead inner frame. LEA-based frame establishment also
needs publication. Normal nested registration helpers
then unwound using that stale frame. This is not exception dispatch or SEH.
"""
import argparse
from pathlib import Path
import runpy
import sys


def install():
    from tools.recomp.lifter import Lifter
    if getattr(Lifter.lift_instruction, '_fnv_frame_restore', False):
        return
    original = Lifter.lift_instruction
    def lift(self, instruction):
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

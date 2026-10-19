"""Preserve normal-frame exchange for a whole-body-audited EH3 helper pair."""
import argparse
from pathlib import Path
import json
import runpy
import sys


def install(report):
    if report.get('abi')!='msvc-eh3-cookie-normal-frame-v1':raise ValueError('Require the normal EH3 helper audit')
    from tools.recomp.translator import FunctionTranslator
    from tools.recomp.lifter import Lifter
    prolog,epilog=report['helpers'];addresses={prolog['address'],epilog['address']};publish=prolog['publish_address']
    prior=getattr(FunctionTranslator.translate_function,'_fnv_eh3',None)
    if prior:
        if prior!=report:raise ValueError('Different EH3 helper pair already installed')
        return
    translate=FunctionTranslator.translate_function;lift=Lifter.lift_instruction
    def translate_function(self,*args,**kwargs):
        self.lifter.SEH_PROLOGS=frozenset({*self.lifter.SEH_PROLOGS,prolog['address']})
        self.lifter.SEH_HELPERS=frozenset({*self.lifter.SEH_HELPERS,*addresses})
        return translate(self,*args,**kwargs)
    def lift_instruction(self,instruction):
        result=lift(self,instruction)
        if instruction.address==publish:
            if instruction.mnemonic!='mov' or instruction.op_str!='ebp, eax':raise ValueError('Audited EH3 publication instruction changed')
            result=[*result,'g_ebp = ebp; g_seh_ebp = ebp; /* audited EH3 frame establishment */']
        return result
    translate_function._fnv_eh3=report
    FunctionTranslator.translate_function=translate_function;Lifter.lift_instruction=lift_instruction


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','analysis','eh3-helpers'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--simd-bits',action='store_true');p.add_argument('--mul-flags',action='store_true')
    a,remaining=p.parse_known_args();sys.path.insert(0,str(a.sdk.resolve()))
    from audit_eh3_helpers import verify
    from aot_frame_restore import install as frame
    if a.simd_bits:
        from aot_simd_bits import install as simd
        simd()
    if a.mul_flags:
        from aot_mul_flags import install as multiply
        multiply()
    frame();install(verify(a.analysis.resolve(),json.loads(a.eh3_helpers.read_text())))
    sys.argv=['tools.recomp',*remaining];runpy.run_module('tools.recomp',run_name='__main__')

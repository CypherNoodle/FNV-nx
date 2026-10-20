"""Opt-in AH flag snapshot for the extended x87 FNSTSW/SAHF path.

OF is preserved by hardware SAHF but not represented by this SDK path. Its
consumers stop explicitly. No new claim of a complete EFLAGS architecture.
"""
import re


def install():
    import tools.recomp.lifter as module
    from tools.recomp.translator import FunctionTranslator
    if getattr(module.Lifter.lift_instruction,'_fnv_sahf_flags',False):return
    if not getattr(module.Lifter.lift_instruction,'_fnv_x87_extended',False):raise ValueError('Extended x87 overlay must be installed first')
    original_lift=module.Lifter.lift_instruction;original_condition=module._make_condition;original_translate=FunctionTranslator.translate_function
    def lift(self,instruction):
        if instruction.mnemonic=='sahf':
            code=[f'_fnv_sahf_flags=(eax>>8)&0xffu; _fnv_sahf_site=0x{instruction.address:08X}u;']
            if self.needs_cf:code.append('_cf=(int)(_fnv_sahf_flags&1u);')
            return code
        return original_lift(self,instruction)
    def condition(jcc,setter,operands):
        if setter!='sahf':return original_condition(jcc,setter,operands)
        cf='(_fnv_sahf_flags&1u)';pf='(_fnv_sahf_flags&4u)';zf='(_fnv_sahf_flags&0x40u)';sf='(_fnv_sahf_flags&0x80u)'
        table={**dict.fromkeys(('jb','jc','jnae'),cf),**dict.fromkeys(('jae','jnb','jnc'),'!'+cf),
               **dict.fromkeys(('je','jz'),zf),**dict.fromkeys(('jne','jnz'),'!'+zf),
               **dict.fromkeys(('jp','jpe'),pf),**dict.fromkeys(('jnp','jpo'),'!'+pf),
               **dict.fromkeys(('jbe','jna'),f'({cf} || {zf})'),**dict.fromkeys(('ja','jnbe'),f'(!{cf} && !{zf})'),
               'js':sf,'jns':'!'+sf}
        if jcc in table:return table[jcc],'AH snapshot from SAHF'
        # Never use the SDK's old FP comparison scalar or an unassigned flag
        # variable for an unknown preserved OF. Mark an explicit codegen gap.
        return '(recomp_unimpl("SAHF overflow flag is not tracked", _fnv_sahf_site), 0)','unsupported SAHF OF consumer'
    def translate(self,address,info):
        code=original_translate(self,address,info)
        if code and '_fnv_sahf_flags' in code:
            code,count=re.subn(r'(void \w+\(void\)\s*\{\n)',r'\1    uint32_t _fnv_sahf_flags=0, _fnv_sahf_site=0; /* captured AH flags */\n',code,count=1)
            if count!=1:raise ValueError('Unexpected function layout for SAHF snapshot')
        return code
    lift._fnv_sahf_flags=True
    for key in ('_fnv_x87_extended','_fnv_x87_environment','_fnv_x87_environment_profile'):
        if hasattr(original_lift,key):setattr(lift,key,getattr(original_lift,key))
    module.Lifter.lift_instruction=lift;module._make_condition=condition;FunctionTranslator.translate_function=translate

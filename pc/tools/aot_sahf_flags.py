"""Opt-in flag snapshots for extended x87 FCOMI and FNSTSW/SAHF paths.

OF is preserved by hardware SAHF but not represented by this SDK path. Its
consumers stop explicitly. No new claim of a complete EFLAGS architecture.
"""
import re

COMPARE_FLAGS=frozenset(('fcomi','fcomip','fucomi','fucomip','fcompi','fucompi'))


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
        code=original_lift(self,instruction)
        if instruction.mnemonic in COMPARE_FLAGS and any('fnv_x87_stack_compare_flags' in line for line in code) and self.needs_cf:
            code.append('_cf=(int)(_fnv_x87_flags&1u);')
        return code
    def condition(jcc,setter,operands):
        if setter!='sahf' and setter not in COMPARE_FLAGS:return original_condition(jcc,setter,operands)
        snapshot='_fnv_sahf_flags' if setter=='sahf' else '_fnv_x87_flags'
        cf=f'({snapshot}&1u)';pf=f'({snapshot}&4u)';zf=f'({snapshot}&0x40u)';sf=f'({snapshot}&0x80u)'
        table={**dict.fromkeys(('jb','jc','jnae'),cf),**dict.fromkeys(('jae','jnb','jnc'),'!'+cf),
               **dict.fromkeys(('je','jz'),zf),**dict.fromkeys(('jne','jnz'),'!'+zf),
               **dict.fromkeys(('jp','jpe'),pf),**dict.fromkeys(('jnp','jpo'),'!'+pf),
               **dict.fromkeys(('jbe','jna'),f'({cf} || {zf})'),**dict.fromkeys(('ja','jnbe'),f'(!{cf} && !{zf})'),
               'js':sf,'jns':'!'+sf}
        if setter in COMPARE_FLAGS:
            # FCOMI/FUCOMI clear OF/SF/AF; SAHF instead preserves OF.
            table.update(jo='0',jno='1',js='0',jns='1',jl='0',jnge='0',jge='1',jnl='1',
                         jle=zf,jng=zf,jg='!'+zf,jnle='!'+zf)
        if jcc in table:return table[jcc],'captured x87/AH flags'
        # Never use the SDK's old FP comparison scalar or an unassigned flag
        # variable for an unknown preserved OF. Mark an explicit codegen gap.
        return '(recomp_unimpl("SAHF overflow flag is not tracked", _fnv_sahf_site), 0)','unsupported SAHF OF consumer'
    def translate(self,address,info):
        code=original_translate(self,address,info)
        declarations=[]
        if code and '_fnv_sahf_flags' in code:declarations.append('uint32_t _fnv_sahf_flags=0, _fnv_sahf_site=0; /* captured AH flags */')
        if code and '_fnv_x87_flags' in code:declarations.append('uint32_t _fnv_x87_flags=0; /* CF/PF/ZF; OF/SF/AF cleared */')
        if declarations:
            code,count=re.subn(r'(void \w+\(void\)\s*\{\n)',lambda match:match[1]+'    '+'\n    '.join(declarations)+'\n',code,count=1)
            if count!=1:raise ValueError('Unexpected function layout for x87 flag snapshot')
        return code
    lift._fnv_sahf_flags=True
    for key in ('_fnv_x87_extended','_fnv_x87_environment','_fnv_x87_environment_profile'):
        if hasattr(original_lift,key):setattr(lift,key,getattr(original_lift,key))
    module.Lifter.lift_instruction=lift;module._make_condition=condition;FunctionTranslator.translate_function=translate

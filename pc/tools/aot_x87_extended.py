"""Experimental exact-payload x87 overlay; not enabled for game generation.

Install AFTER aot_frame_restore in an isolated experiment. Requires
x87_stack.h and a per-context `fnv_x87_state` variable. All other x87 forms
fail explicitly: never mix this stack with the SDK's binary64-backed stack.
"""


def emit(instruction):
    from tools.recomp.lifter import _fmt_mem
    m,ops=instruction.mnemonic,instruction.operands
    call=None
    if m in ('fld','fstp') and len(ops)==1 and ops[0].type=='mem' and ops[0].mem_size==10:
        address=f'(unsigned char *)(g_xbox_mem_offset + (uint32_t)({_fmt_mem(ops[0])}))'
        name='load' if m=='fld' else 'store_pop'
        call=f'fnv_x87_stack_{name}(&fnv_x87_state,{address})'
    elif m in ('fld','fst','fstp') and len(ops)==1 and ops[0].type=='mem' and ops[0].mem_size in (4,8):
        address=f'(unsigned char *)(g_xbox_mem_offset + (uint32_t)({_fmt_mem(ops[0])}))'
        if m=='fld':call=f'fnv_x87_stack_load_binary(&fnv_x87_state,{address},{ops[0].mem_size})'
        else:call=f'fnv_x87_stack_store_binary(&fnv_x87_state,{address},{ops[0].mem_size},{int(m=="fstp")})'
    elif m=='fsqrt' and not ops:
        call='fnv_x87_stack_sqrt(&fnv_x87_state)'
    elif m in ('faddp','fsubp','fmulp','fdivp'):
        # Only ST(1) <- ST(1) op ST(0), then pop, is implemented here.
        normalized=[op.reg.replace('(','').replace(')','') if op.type=='reg' and op.reg else None for op in ops]
        if normalized in ([],['st1'],['st1','st0']):
            operation={'faddp':'ADD','fsubp':'SUB','fmulp':'MUL','fdivp':'DIV'}[m]
            call=f'fnv_x87_stack_binary_pop(&fnv_x87_state,FNV_X87_{operation})'
    if call is None:return None
    return [f'if ({call} != FNV_X87_STACK_OK) {{ RECOMP_UNIMPL("extended x87 operand/state unsupported", 0x{instruction.address:08X}u); }}']


def install():
    from tools.recomp.lifter import Lifter
    if getattr(Lifter.lift_instruction,'_fnv_x87_extended',False):return
    original=Lifter.lift_instruction
    def lift(self,instruction):
        if instruction.mnemonic.startswith('f') or instruction.mnemonic=='wait':
            code=emit(instruction)
            return code if code is not None else self._unimplemented(
                instruction,instruction.mnemonic,' (experimental extended stack form not implemented)')
        return original(self,instruction)
    lift._fnv_x87_extended=True
    Lifter.lift_instruction=lift

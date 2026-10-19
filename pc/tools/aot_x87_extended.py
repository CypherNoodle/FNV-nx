"""Experimental exact-payload x87 overlay; not enabled for game generation.

Install AFTER aot_frame_restore in an isolated experiment. Requires
x87_stack.h and a per-context `fnv_x87_state` variable. All other x87 forms
fail explicitly: never mix this stack with the SDK's binary64-backed stack.
"""


def emit(instruction):
    from tools.recomp.lifter import _fmt_mem
    m,ops=instruction.mnemonic,instruction.operands
    call=None
    def register_index(op):
        if op.type!='reg' or not op.reg:return None
        name=op.reg.replace('(','').replace(')','')
        return int(name[2]) if len(name)==3 and name[:2]=='st' and name[2] in '01234567' else None
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
    elif m=='frndint' and not ops:
        call='fnv_x87_stack_round_integer(&fnv_x87_state)'
    elif m in ('fild','fist','fistp','fisttp') and len(ops)==1 and ops[0].type=='mem' and ops[0].mem_size in (2,4,8):
        address=f'(unsigned char *)(g_xbox_mem_offset + (uint32_t)({_fmt_mem(ops[0])}))'
        if m=='fild':call=f'fnv_x87_stack_load_integer(&fnv_x87_state,{address},{ops[0].mem_size})'
        else:call=f'fnv_x87_stack_store_integer(&fnv_x87_state,{address},{ops[0].mem_size},{int(m!="fist")},{int(m=="fisttp")})'
    elif m in ('fld1','fldz') and not ops:
        call=f'fnv_x87_stack_constant(&fnv_x87_state,{int(m=="fld1")})'
    elif m=='fxch' and (not ops or (len(ops)==1 and register_index(ops[0]) is not None)):
        call=f'fnv_x87_stack_exchange(&fnv_x87_state,{register_index(ops[0]) if ops else 1})'
    elif m=='fxch' and len(ops)==2 and register_index(ops[0])==0 and register_index(ops[1]) is not None:
        # SDK decoding materializes the implicit ST(0) operand.
        call=f'fnv_x87_stack_exchange(&fnv_x87_state,{register_index(ops[1])})'
    elif m in ('fld','fst','fstp') and len(ops)==1 and register_index(ops[0]) is not None:
        index=register_index(ops[0])
        if m=='fld':call=f'fnv_x87_stack_duplicate(&fnv_x87_state,{index})'
        else:call=f'fnv_x87_stack_store_register(&fnv_x87_state,{index},{int(m=="fstp")})'
    elif m in ('fcompp','fucompp') and not ops:
        call=f'fnv_x87_stack_compare(&fnv_x87_state,1,{int(m=="fucompp")},2)'
    elif m in ('fcom','fcomp') and len(ops)==1 and ops[0].type=='mem' and ops[0].mem_size in (4,8):
        call=f'fnv_x87_stack_compare_memory(&fnv_x87_state,(const unsigned char *)(g_xbox_mem_offset+(uint32_t)({_fmt_mem(ops[0])})),{ops[0].mem_size},{int(m=="fcomp")})'
    elif m=='ftst' and not ops:
        call='fnv_x87_stack_test_zero(&fnv_x87_state)'
    elif m in ('fchs','fabs') and not ops:
        call=f'fnv_x87_stack_sign(&fnv_x87_state,{int(m=="fabs")})'
    elif m in ('fcom','fcomp','fucom','fucomp') and (not ops or (len(ops)==1 and register_index(ops[0]) is not None)):
        call=f'fnv_x87_stack_compare(&fnv_x87_state,{register_index(ops[0]) if ops else 1},{int(m.startswith("fu"))},{int(m.endswith("p"))})'
    elif m=='fldcw' and len(ops)==1 and ops[0].type=='mem' and ops[0].mem_size==2:
        call=f'fnv_x87_stack_control(&fnv_x87_state,(const unsigned char *)(g_xbox_mem_offset + (uint32_t)({_fmt_mem(ops[0])})))'
    elif m in ('fnstcw','fnstsw') and len(ops)==1 and ops[0].type=='mem' and ops[0].mem_size==2:
        field='control' if m=='fnstcw' else 'status'
        return [f'{{ unsigned char *_fnv_out=(unsigned char *)(g_xbox_mem_offset+(uint32_t)({_fmt_mem(ops[0])})); _fnv_out[0]=(unsigned char)fnv_x87_state.{field}; _fnv_out[1]=(unsigned char)(fnv_x87_state.{field}>>8); }}']
    elif m=='fnstsw' and len(ops)==1 and ops[0].type=='reg' and ops[0].reg=='ax':
        return ['eax=(eax&0xffff0000u)|(uint32_t)fnv_x87_state.status;']
    elif m=='fnclex' and not ops:
        return ['fnv_x87_stack_clear_exceptions(&fnv_x87_state);']
    elif m=='fninit' and not ops:
        return ['fnv_x87_stack_init(&fnv_x87_state);']
    elif m in ('fadd','fmul','fsub','fsubr','fdiv','fdivr','faddp','fmulp','fsubp','fsubrp','fdivp','fdivrp',
               'fiadd','fimul','fisub','fisubr','fidiv','fidivr'):
        integer=m.startswith('fi');base=m[2:] if integer else m[1:]
        pop=base.endswith('p');base=base[:-1] if pop else base
        reverse=base.endswith('r');base=base[:-1] if reverse else base
        operation={'add':'ADD','sub':'SUB','mul':'MUL','div':'DIV'}[base]
        if len(ops)==1 and ops[0].type=='mem' and not pop and (
                ops[0].mem_size in ((2,4) if integer else (4,8))):
            address=f'(const unsigned char *)(g_xbox_mem_offset+(uint32_t)({_fmt_mem(ops[0])}))'
            call=f'fnv_x87_stack_binary_memory(&fnv_x87_state,FNV_X87_{operation},{address},{ops[0].mem_size},{int(integer)},{int(reverse)})'
        elif not integer:
            indices=[register_index(op) for op in ops]
            destination=source=None
            if not ops:destination,source=(1,0) if pop else (0,1)
            elif len(indices)==1 and indices[0] is not None:destination,source=(indices[0],0) if pop else (0,indices[0])
            elif len(indices)==2 and None not in indices and 0 in indices:destination,source=indices
            if destination is not None:
                call=f'fnv_x87_stack_binary_register(&fnv_x87_state,FNV_X87_{operation},{destination},{source},{int(pop)},{int(reverse)})'
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

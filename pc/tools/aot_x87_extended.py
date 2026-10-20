"""Experimental exact-payload x87 overlay; not enabled for game generation.

Install AFTER aot_frame_restore in an isolated experiment. Requires
x87_stack.h and a per-context `fnv_x87_state` variable. All other x87 forms
fail explicitly: never mix this stack with the SDK's binary64-backed stack.
"""
_transcendental_model=None
_model_configured=False


def configure_transcendentals(model):
    from x87_models import MODELS
    global _transcendental_model,_model_configured
    if model not in (None,*MODELS):raise ValueError('Unknown x87 numerical model')
    if _model_configured and model!=_transcendental_model:raise ValueError('Cannot mix x87 numerical profiles in one process')
    _transcendental_model=model;_model_configured=True


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
    elif m in ('f2xm1','fpatan','fyl2x') and not ops and _transcendental_model is not None:
        op={'f2xm1':'F2XM1','fpatan':'FPATAN','fyl2x':'FYL2X'}[m]
        call=f'fnv_x87_transcendental_candidate(&fnv_x87_state,FNV_X87_CANDIDATE_{op})'
    elif m in ('fsin','fcos','fptan','fsincos') and not ops:
        from x87_models import TRIG_MODEL
        if _transcendental_model==TRIG_MODEL:call=f'fnv_x87_trig_candidate(&fnv_x87_state,FNV_X87_TRIG_{m.upper()})'
    elif m=='fsqrt' and not ops:
        call='fnv_x87_stack_sqrt(&fnv_x87_state)'
    elif m=='fscale' and not ops:
        call='fnv_x87_stack_scale(&fnv_x87_state)'
    elif m in ('fprem','fprem1') and not ops:
        call=f'fnv_x87_stack_remainder(&fnv_x87_state,{int(m=="fprem1")})'
    elif m=='frndint' and not ops:
        call='fnv_x87_stack_round_integer(&fnv_x87_state)'
    elif m=='fbstp' and len(ops)==1 and ops[0].type=='mem' and ops[0].mem_size==10:
        call=f'fnv_x87_stack_store_bcd(&fnv_x87_state,(unsigned char *)(g_xbox_mem_offset+(uint32_t)({_fmt_mem(ops[0])})))'
    elif m in ('fild','fist','fistp','fisttp') and len(ops)==1 and ops[0].type=='mem' and ops[0].mem_size in (2,4,8):
        address=f'(unsigned char *)(g_xbox_mem_offset + (uint32_t)({_fmt_mem(ops[0])}))'
        if m=='fild':call=f'fnv_x87_stack_load_integer(&fnv_x87_state,{address},{ops[0].mem_size})'
        else:call=f'fnv_x87_stack_store_integer(&fnv_x87_state,{address},{ops[0].mem_size},{int(m!="fist")},{int(m=="fisttp")})'
    elif m in ('fld1','fldz') and not ops:
        call=f'fnv_x87_stack_constant(&fnv_x87_state,{int(m=="fld1")})'
    elif m in ('fldpi','fldl2e','fldl2t','fldlg2','fldln2') and not ops:
        kind={'fldpi':'PI','fldl2e':'LOG2E','fldl2t':'LOG2TEN','fldlg2':'LOG10TWO','fldln2':'LN2'}[m]
        call=f'fnv_x87_stack_math_constant(&fnv_x87_state,FNV_X87_{kind})'
    elif m in ('wait','fwait','fnop') and not ops:
        call='fnv_x87_stack_wait(&fnv_x87_state)'
    elif m=='fxam' and not ops:
        call='fnv_x87_stack_examine(&fnv_x87_state)'
    elif m=='ffree' and len(ops)==1 and register_index(ops[0]) is not None:
        call=f'fnv_x87_stack_free(&fnv_x87_state,{register_index(ops[0])})'
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
    elif m in ('fcomi','fcomip','fucomi','fucomip','fcompi','fucompi'):
        index=None
        if len(ops)==1:index=register_index(ops[0])
        elif len(ops)==2 and register_index(ops[0])==0:index=register_index(ops[1])
        if index is not None:
            pop=m in ('fcomip','fucomip','fcompi','fucompi')
            call=f'fnv_x87_stack_compare_flags(&fnv_x87_state,{index},{int(m.startswith("fu"))},{int(pop)},&_fnv_x87_flags)'
    elif m in ('fcom','fcomp') and len(ops)==1 and ops[0].type=='mem' and ops[0].mem_size in (4,8):
        call=f'fnv_x87_stack_compare_memory(&fnv_x87_state,(const unsigned char *)(g_xbox_mem_offset+(uint32_t)({_fmt_mem(ops[0])})),{ops[0].mem_size},{int(m=="fcomp")})'
    elif m in ('ficom','ficomp') and len(ops)==1 and ops[0].type=='mem' and ops[0].mem_size in (2,4):
        call=f'fnv_x87_stack_compare_integer(&fnv_x87_state,(const unsigned char *)(g_xbox_mem_offset+(uint32_t)({_fmt_mem(ops[0])})),{ops[0].mem_size},{int(m=="ficomp")})'
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
        return ['fnv_x87_stack_reset(&fnv_x87_state);']
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


def install_environment(*, code_selector, data_selector, stack_selector,
                        opcode_compatibility, data_pointer_on_exception_only,extra_selector=None):
    """Opt-in protected-mode environment tracking, after install().

    Selectors and architectural FOP/FDP policy must be chosen explicitly from
    the guest CPU contract. No host CPUID passthrough or guessed Windows GDT.
    Requires per-context `fnv_x87_environment_state` in addition to stack state.
    The masked staging core never delivers an unmasked exception; FOP/FDP
    exception-only policies therefore retain those fields on numeric success.
    """
    for value in (code_selector,data_selector,stack_selector):
        if type(value) is not int or not 0<=value<=0xffff:
            raise ValueError('Explicit 16-bit guest selectors required')
    if type(opcode_compatibility) is not bool or type(data_pointer_on_exception_only) is not bool:
        raise ValueError('Explicit guest FOP/FDP policies required')
    if extra_selector is not None and (type(extra_selector) is not int or extra_selector!=data_selector):
        raise ValueError('The flat ES contract requires ES to alias the exact DS selector')
    from tools.recomp.lifter import Lifter, _fmt_mem
    original=Lifter.lift_instruction
    if not getattr(original,'_fnv_x87_extended',False):
        raise ValueError('Install extended stack overlay first, exactly once')
    if getattr(original,'_fnv_x87_environment',False):
        raise ValueError('Environment overlay already installed')
    control={'fnclex','fldcw','fnstcw','fnstsw','fnstenv','fldenv','fnsave','frstor','wait','fwait','fninit'}
    def lift(self,instruction):
        m=instruction.mnemonic
        if not (m.startswith('f') or m=='wait'):return original(self,instruction)
        raw=bytes.fromhex(instruction.bytes_hex)
        prefix=[];index=0
        while index<len(raw) and raw[index] in (0x26,0x2e,0x36,0x3e,0x64,0x65,0x66,0x67,0xf0,0xf2,0xf3):
            prefix.append(raw[index]);index+=1
        mem=[o for o in instruction.operands if o.type=='mem']
        # FS/GS bases and 16-bit addressing require a separate audited contract.
        segments=(None,'cs','ds','ss')+(('es',) if extra_selector is not None else ())
        if 0x67 in prefix or any(p in prefix for p in (0xf0,0xf2,0xf3)) or any(o.mem_seg not in segments for o in mem):
            return self._unimplemented(instruction,m,' (unsupported x87 environment addressing)')
        if m in ('fnstenv','fldenv','fnsave','frstor'):
            # Capstone reports the same env width with 66, and incorrectly
            # reports full-state operands as four bytes. Inspect the actual
            # operand-size prefix rather than trusting those width fields.
            if len(mem)!=1 or len(instruction.operands)!=1 or 0x66 in prefix:
                return self._unimplemented(instruction,m,' (only 32-bit protected x87 images supported)')
            full=int(m in ('fnsave','frstor'));size=108 if full else 28
            op='save32' if m in ('fnstenv','fnsave') else 'restore32'
            address=f'(unsigned char *)(g_xbox_mem_offset+(uint32_t)({_fmt_mem(mem[0])}))'
            return [f'if (fnv_x87_stack_{op}(&fnv_x87_state,&fnv_x87_environment_state,{address},{size},{full}) != FNV_X87_STACK_OK) {{ RECOMP_UNIMPL("extended x87 environment unsupported", 0x{instruction.address:08X}u); }}']
        code=emit(instruction)
        if code is None:return original(self,instruction)
        if m=='fninit':return code+['fnv_x87_environment_state=(struct fnv_x87_environment){0};']
        if m in control:return code
        if not 0<=instruction.address<=0xffffffff or index+1>=len(raw) or not 0xd8<=raw[index]<=0xdf:
            return self._unimplemented(instruction,m,' (invalid x87 opcode metadata)')
        updates=[f'fnv_x87_environment_state.instruction_pointer=0x{instruction.address:08X}u;',
                 f'fnv_x87_environment_state.code_selector=0x{code_selector:04X}u;']
        if opcode_compatibility:
            opcode=((raw[index]&7)<<8)|raw[index+1]
            updates.append(f'fnv_x87_environment_state.opcode=0x{opcode:03X}u;')
        if mem and not data_pointer_on_exception_only:
            operand=mem[0];segment=operand.mem_seg or ('ss' if operand.mem_base in ('esp','ebp') else 'ds')
            selector={'cs':code_selector,'ss':stack_selector,'ds':data_selector,'es':extra_selector}[segment]
            updates.extend([f'fnv_x87_environment_state.data_pointer=(uint32_t)({_fmt_mem(operand)});',
                            f'fnv_x87_environment_state.data_selector=0x{selector:04X}u;'])
        return code+updates
    lift._fnv_x87_extended=True
    lift._fnv_x87_environment=True
    lift._fnv_x87_environment_profile=(code_selector,data_selector,stack_selector,opcode_compatibility,data_pointer_on_exception_only,extra_selector)
    Lifter.lift_instruction=lift

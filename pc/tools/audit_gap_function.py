"""Audit one explicitly observed x86 entry in a gap of a frozen function list.

This does not discover arbitrary functions or authorize generated publication.
Input images, reports, addresses and any generated code stay private.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

def audit(code,base,start,limit,functions,reference=None):
    import capstone as cs
    from capstone.x86_const import X86_OP_IMM
    if not 0<=base<=start<limit<=base+len(code) or limit>0x100000000:raise ValueError('Invalid bounded code range')
    intervals=[];starts=set()
    for row in functions:
        a=int(row['start'],0) if isinstance(row['start'],str) else row['start']
        b=int(row['end'],0) if isinstance(row['end'],str) else row['end']
        if not 0<=a<b<=0x100000000:raise ValueError('Invalid existing function range')
        if a<limit and start<b:intervals.append((a,b))
        starts.add(a)
        if a<=start<b:raise ValueError('Entry already owned by an existing function')
    decoder=cs.Cs(cs.CS_ARCH_X86,cs.CS_MODE_32);decoder.detail=True
    pending=[start];decoded={};occupied={};calls=set();tails=set();returns=set();indirect_calls=0
    while pending:
        at=pending.pop()
        if at in decoded:continue
        if not start<=at<limit:raise ValueError('Control flow leaves the explicit gap')
        if at in occupied:raise ValueError('Branch enters an instruction interior')
        instruction=next(decoder.disasm(code[at-base:min(limit-base,at-base+15)],at,count=1),None)
        if not instruction or instruction.size==0:raise ValueError('Undecodable reachable instruction')
        end=at+instruction.size
        if any(a<end and at<b for a,b in intervals):raise ValueError('Reachable code overlaps an existing function')
        if any(i in occupied for i in range(at,end)):raise ValueError('Overlapping instruction decode')
        decoded[at]=instruction
        for i in range(at,end):occupied[i]=at
        if len(decoded)>65536:raise ValueError('Instruction budget exceeded')
        if instruction.mnemonic in ('int3','ud2','hlt') or instruction.group(cs.CS_GRP_INT):raise ValueError('Reachable trap or interrupt requires a separate audit')
        if instruction.group(cs.CS_GRP_RET):
            if instruction.mnemonic not in ('ret','repz ret'):raise ValueError('Unsupported return shape')
            returns.add(at);continue
        if instruction.group(cs.CS_GRP_CALL):
            if instruction.operands and instruction.operands[0].type==X86_OP_IMM:calls.add(instruction.operands[0].imm&0xffffffff)
            else:indirect_calls+=1
        if instruction.group(cs.CS_GRP_JUMP):
            if not instruction.operands or instruction.operands[0].type!=X86_OP_IMM:raise ValueError('Indirect jump requires an explicit table audit')
            target=instruction.operands[0].imm&0xffffffff
            if start<=target<limit:pending.append(target)
            elif instruction.mnemonic=='jmp' and target in starts:tails.add(target)
            else:raise ValueError('Unverified branch outside the gap')
            if instruction.mnemonic=='jmp':continue
        pending.append(end)
    if not returns and not tails:raise ValueError('No reachable return or verified tail exit')
    finish=max(a+i.size for a,i in decoded.items());pattern=struct.pack('<I',start)
    offsets=[]
    if reference is not None:
        at=reference.find(pattern)
        while at>=0:
            if at%4==0:offsets.append(at)
            at=reference.find(pattern,at+1)
    return dict(status='AUDITED_GAP_ENTRY',start=start,end=finish,size=finish-start,
                instructions=len(decoded),reachable_bytes=len(occupied),returns=sorted(returns),
                calls=sorted(calls),indirect_calls=indirect_calls,tail_exits=sorted(tails),
                aligned_reference_file_offsets=offsets,
                code_sha256=hashlib.sha256(code).hexdigest(),
                reference_sha256=hashlib.sha256(reference).hexdigest() if reference is not None else None,
                translation_verified=False,engine_started=False)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('code','functions','output'):p.add_argument('--'+n,type=Path,required=True)
    for n in ('base','start','limit'):p.add_argument('--'+n,type=lambda s:int(s,0),required=True)
    p.add_argument('--reference',type=Path);a=p.parse_args()
    if a.output.exists():raise FileExistsError(a.output)
    report=audit(a.code.read_bytes(),a.base,a.start,a.limit,json.loads(a.functions.read_text()),a.reference.read_bytes() if a.reference else None)
    report['functions_sha256']=hashlib.sha256(a.functions.read_bytes()).hexdigest()
    with a.output.open('x',encoding='ascii') as stream:json.dump(report,stream,indent=2);stream.write('\n')

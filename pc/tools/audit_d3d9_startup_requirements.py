"""Read-only static audit of a bounded x86 D3D9 startup rejection pattern.

Requires exact hashes for the EXE and caller-provided code capture. It neither
patches caps nor executes code. Output contains addresses/metadata, never code
bytes or copied strings. Failure to recognize a pattern is an error, not proof
of a capability requirement being absent.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from audit_install import pe_info


def audit(exe,capture,expected_exe,expected_capture,base,error_text,python_deps=None):
    if python_deps:
        import sys
        sys.path.insert(0,str(python_deps.resolve()))
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    from capstone.x86_const import X86_OP_REG,X86_OP_MEM,X86_OP_IMM
    raw=exe.read_bytes();code=capture.read_bytes();digest=lambda x:hashlib.sha256(x).hexdigest()
    if digest(raw)!=expected_exe or digest(code)!=expected_capture:raise ValueError('input identity mismatch')
    if base<0 or base+len(code)>0x100000000:raise ValueError('capture address range')
    pe=pe_info(exe)
    def file_offset(va,size):
        for s in pe['sections']:
            rel=va-pe['image_base']-s['rva']
            if 0<=rel and rel+size<=s['raw_bytes']:return s['raw_offset']+rel
        raise ValueError('required file value has no section backing')
    needle=error_text.encode('ascii')+b'\0';at=raw.find(needle)
    if at<0 or raw.find(needle,at+1)>=0:raise ValueError('error string missing or ambiguous')
    va=None
    for s in pe['sections']:
        if s['raw_offset']<=at and at+len(needle)<=s['raw_offset']+s['raw_bytes']:va=pe['image_base']+s['rva']+at-s['raw_offset']
    if va is None:raise ValueError('error string is outside file sections')
    md=Cs(CS_ARCH_X86,CS_MODE_32);md.detail=True
    refs=[];pointer=struct.pack('<I',va);pos=0
    while True:
        pos=code.find(pointer,pos)
        if pos<0:break
        # Only a complete push imm32 referencing this exact literal qualifies.
        if pos and code[pos-1]==0x68:refs.append(base+pos-1)
        pos+=4
    if len(refs)!=1:raise ValueError('expected one push reference to rejection literal')
    ref=refs[0];gates=[]
    def mem_at(op,disp):return op.type==X86_OP_MEM and op.mem.base and not op.mem.index and op.mem.disp==disp
    for start in range(max(base,ref-512),ref):
        ins=list(md.disasm(code[start-base:ref-base+5],start))
        if len(ins)<2 or ins[-1].address!=ref:continue
        a,b=ins[:2]
        if a.mnemonic!='mov' or len(a.operands)!=2 or a.operands[0].type!=X86_OP_REG or not mem_at(a.operands[1],28):continue
        register=a.operands[0].reg
        if b.mnemonic!='and' or b.operands[0].type!=X86_OP_REG or b.operands[0].reg!=register or b.operands[1].type!=X86_OP_IMM or b.operands[1].imm!=0x10000:continue
        if not any(x.mnemonic in ('je','jz') and x.operands[0].type==X86_OP_IMM and x.operands[0].imm==ref for x in ins[2:]):continue
        stages=[]
        for n,x in enumerate(ins):
            if x.mnemonic!='mov' or len(x.operands)!=2 or x.operands[0].type!=X86_OP_REG:continue
            field=next((offset for offset in (196,204) if mem_at(x.operands[1],offset)),None)
            if field is None or n+4>=len(ins):continue
            mask,call,compare,branch=ins[n+1:n+5]
            if mask.mnemonic!='and' or mask.operands[0].reg!=x.operands[0].reg or mask.operands[1].type!=X86_OP_IMM or mask.operands[1].imm!=65535:continue
            if call.mnemonic!='call' or call.operands[0].type!=X86_OP_IMM:continue
            if compare.mnemonic!='cmp' or compare.operands[0].type!=X86_OP_REG or compare.operands[0].reg!=x.operands[0].reg or compare.op_str.split(',')[-1].strip()!='eax' or branch.mnemonic!='jb':continue
            helper=call.operands[0].imm
            if not base<=helper<base+len(code):raise ValueError('threshold helper outside capture')
            helper_ins=list(md.disasm(code[helper-base:helper-base+24],helper));loads=[];body=[]
            for h in helper_ins:
                body.append(h)
                if h.mnemonic=='ret':break
                if h.mnemonic=='mov' and len(h.operands)==2 and h.op_str.startswith('eax,') and h.operands[1].type==X86_OP_MEM and not h.operands[1].mem.base and not h.operands[1].mem.index:loads.append(h.operands[1].mem.disp&0xffffffff)
            if [x.mnemonic for x in body]!=['push','mov','mov','pop','ret'] or [body[0].op_str,body[1].op_str,body[3].op_str]!=['ebp','ebp, esp','ebp'] or body[-1].op_str:raise ValueError('threshold helper has unsupported side effects')
            if len(loads)!=1:raise ValueError('threshold helper is not a single absolute getter')
            threshold_address=loads[0];value=struct.unpack_from('<I',raw,file_offset(threshold_address,4))[0]
            stages.append(dict(stage='vertex' if field==196 else 'pixel',caps_offset=field,version_mask=65535,comparison='unsigned_lower_rejects',threshold_address=threshold_address,initial_required_version=value,initial_major=(value>>8)&255,initial_minor=value&255))
        if len(stages)!=2 or {s['stage'] for s in stages}!={'vertex','pixel'}:continue
        gates.append(dict(caps_offset=28,required_mask=65536,shader_version_checks=stages))
    if len(gates)!=1:raise ValueError('capability gate pattern missing or ambiguous')
    if digest(exe.read_bytes())!=expected_exe or digest(capture.read_bytes())!=expected_capture:raise ValueError('inputs changed during audit')
    return dict(status='PASS',schema='fnv-static-d3d9-startup-gate-v1',executable_sha256=expected_exe,capture_sha256=expected_capture,capture_base=base,
                gate=gates[0],static_only=True,thresholds_may_change_at_runtime=True,code_executed=False,caps_modified=False,engine_started=False,tool_sha256=digest(Path(__file__).read_bytes()))


def run(a):
    if a.output.exists():raise ValueError('use a fresh output directory')
    report=audit(a.exe,a.capture,a.exe_sha256,a.capture_sha256,a.capture_base,a.error_text,a.python_deps)
    a.output.mkdir(parents=True);(a.output/'requirements.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('exe','capture','output'):p.add_argument('--'+n,type=Path,required=True)
    for n in ('exe-sha256','capture-sha256','error-text'):p.add_argument('--'+n,required=True)
    p.add_argument('--capture-base',type=lambda s:int(s,0),required=True);p.add_argument('--python-deps',type=Path);run(p.parse_args())

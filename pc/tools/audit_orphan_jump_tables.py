"""Read-only census of orphan hotpatch seeds backed by real indexed dispatches.

Reports contain private addresses; this tool never edits a scan or registers code.
"""
import argparse
from bisect import bisect_left
import json
from pathlib import Path
import struct
import sys

from aot_code_data import (executable_address,instruction_boundary_in_known_owner,
                          load_inputs,padding,read_va)
from audit_aot_gaps import Intervals,address,sections_from_container
from run_aot_analysis import sha


def established(function):
    return (function.get('detection_method')=='prologue' and function.get('has_prologue')) or (
        function.get('detection_method')=='call_target' and bool(function.get('called_by')))


def reachable_dispatch(raw,sections,owner,site,decoder):
    """Require a decoded direct path from an established entry to the dispatch."""
    start,end=address(owner['start']),address(owner['end'])
    if not established(owner) or not start<=site<site+7<=end or site-start>4096:return False
    code=read_va(raw,sections,start,site+7-start)
    if code is None:return False
    insns=list(decoder.disasm(code,start));by_address={i.address:i for i in insns}
    if site not in by_address or sum(i.size for i in insns)!=len(code):return False
    from capstone import CS_GRP_JUMP,CS_GRP_RET,CS_GRP_INT,CS_GRP_IRET,CS_GRP_PRIVILEGE
    from capstone.x86_const import X86_OP_IMM
    pending=[start];seen=set()
    while pending:
        at=pending.pop()
        if at in seen or at not in by_address:continue
        seen.add(at)
        if at==site:return True
        i=by_address[at]
        if any(i.group(g) for g in (CS_GRP_RET,CS_GRP_INT,CS_GRP_IRET,CS_GRP_PRIVILEGE)) or i.mnemonic in ('ud2','hlt'):
            continue
        if i.group(CS_GRP_JUMP) or i.mnemonic.startswith('loop'):
            if len(i.operands)==1 and i.operands[0].type==X86_OP_IMM:
                pending.append(i.operands[0].imm&0xffffffff)
            if i.mnemonic=='jmp':continue
        pending.append(at+i.size)
    return False


def find_tables(raw,sections,functions,xrefs,decoder):
    """Conjunctive data evidence; never treats a successful decode as proof."""
    from capstone.x86_const import X86_OP_MEM
    ordered=sorted((address(f['start']),address(f['end']),f) for f in functions if established(f))
    starts=[x[0] for x in ordered];maximum=[];end=0
    for _,stop,_ in ordered:end=max(end,stop);maximum.append(end)
    by_start={address(f['start']):f for f in functions};all_starts=sorted(by_start)
    ranges=Intervals(functions)
    ref_points=sorted({address(r['from']) for r in xrefs}|{address(r['to']) for r in xrefs})
    candidates=[]
    for function in functions:
        start,stop=address(function['start']),address(function['end'])
        if (function.get('detection_method')!='gap_prologue' or function.get('has_prologue') or
            function.get('called_by') or function.get('calls_to') or not 14<=stop-start<=65536):continue
        at=bisect_left(ref_points,start)
        if at<len(ref_points) and ref_points[at]<stop:continue
        occupied=all_starts[bisect_left(all_starts,start):bisect_left(all_starts,stop)]
        if any(p!=start for p in occupied):continue
        blob=read_va(raw,sections,start,stop-start)
        if blob is None or blob[:2]!=bytes.fromhex('8bff'):continue
        cursor=2;targets=[]
        while cursor+4<=len(blob):
            target=struct.unpack_from('<I',blob,cursor)[0]
            if not executable_address(sections,target):break
            targets.append(target);cursor+=4
        if len(targets)<3 or not padding(blob[cursor:]):continue
        if any(start<=t<stop or not instruction_boundary_in_known_owner(
            raw,sections,functions,t,decoder,starts,maximum,ordered) for t in set(targets)):continue
        table=start+2;needle=struct.pack('<I',table);dispatches=[]
        for section in sections:
            if not section['executable']:continue
            section_bytes=raw[section['offset']:section['offset']+section['stored']]
            offset=section_bytes.find(needle)
            while offset>=0:
                site=section['start']+offset-3
                original=read_va(raw,sections,site,7)
                instruction=next(decoder.disasm(original,site,count=1),None) if original else None
                if (instruction is not None and instruction.size==7 and instruction.mnemonic=='jmp' and
                    len(instruction.operands)==1 and instruction.operands[0].type==X86_OP_MEM):
                    mem=instruction.operands[0].mem
                    if not mem.base and not mem.segment and mem.index and mem.scale==4 and mem.disp&0xffffffff==table:
                        for owner_start,_ in ranges.owners(site):
                            owner=by_start[owner_start]
                            if reachable_dispatch(raw,sections,owner,site,decoder):
                                dispatches.append(dict(site=site,owner=owner_start));break
                offset=section_bytes.find(needle,offset+1)
        if not dispatches:continue
        candidates.append(dict(function=function,reason='orphan-indexed-dispatch-pointer-table',
            table_start=table,table_end=start+cursor,entries=len(targets),targets=targets,
            table_sha256=sha_bytes(blob[2:cursor]),dispatches=dispatches))
    return candidates


def sha_bytes(data):
    import hashlib
    return hashlib.sha256(data).hexdigest()


def audit(analysis,output,deps):
    if output.exists():raise ValueError('Fresh private output required')
    sys.path.insert(0,str(deps.resolve()))
    import capstone
    raw,profile,scan,files,identity=load_inputs(analysis)
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
    sources=[Path(__file__).resolve(),Path(__file__).with_name('aot_code_data.py'),Path(__file__).with_name('audit_aot_gaps.py')]
    frozen={str(p):sha(p) for p in sources}
    candidates=find_tables(raw,sections_from_container(raw,profile),json.loads(files['functions.json']),
                           json.loads(files['xrefs.json']),decoder)
    if any(sha(Path(p))!=h for p,h in frozen.items()) or load_inputs(analysis)[:4]!=(raw,profile,scan,files):
        raise ValueError('Audit inputs changed')
    report=dict(status='READ_ONLY_TABLE_CANDIDATES',identity=identity,source_sha256=frozen,
        candidates=candidates,scan_modified=False,entries_registered=0,engine_started=False,
        limits=['No selection changes or generated-code validation','Dispatch reachability is local control flow, not whole-program execution'])
    output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(report['status'],len(candidates),'tables; scan_modified=False')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('analysis','output','python-deps'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();audit(a.analysis.resolve(),a.output.resolve(),a.python_deps)

"""Read-only census of bounded missing entry candidates in a private generation.

Nothing is registered or translated. Detailed evidence contains private game
addresses and bytes. A candidate still needs ABI/reachability validation.
"""
import argparse
from bisect import bisect_right
from collections import Counter,defaultdict
import json
from pathlib import Path
import re
import sys
from aot_code_data import load_inputs,read_va
from audit_aot_gaps import Intervals,address,sections_from_container,source_references
from run_aot_analysis import generation_paths,sha


def assess_gap(raw,sections,functions,target,decoder):
    ranges=Intervals(functions)
    if ranges.owners(target):return None,'overlaps-indexed-function'
    starts=sorted({address(f['start']) for f in functions});i=bisect_right(starts,target)
    if i==len(starts):return None,'no-following-indexed-boundary'
    end=starts[i]
    if not 0<end-target<=512:return None,'gap-size-limit'
    home=next((s for s in sections if s['executable'] and s['start']<=target<end<=s['end']),None)
    if home is None:return None,'outside-executable-storage'
    previous=[f for f in functions if address(f['end'])==target and target-address(f['start'])<=8192]
    predecessors=[]
    for f in previous:
        start=address(f['start']);code=read_va(raw,sections,start,target-start)
        if code is None:continue
        instructions=list(decoder.disasm(code,start))
        if (instructions and sum(x.size for x in instructions)==len(code) and
            instructions[-1].address+instructions[-1].size==target and instructions[-1].mnemonic=='ret'):
            predecessors.append(start)
    if not predecessors:return None,'no-complete-preceding-ret-boundary'
    code=read_va(raw,sections,target,end-target)
    if code is None:return None,'not-file-backed'
    instructions=list(decoder.disasm(code,target))
    if not instructions or sum(x.size for x in instructions)!=len(code) or instructions[-1].mnemonic!='ret':
        return None,'gap-not-complete-ret-body'
    from capstone import CS_GRP_JUMP,CS_GRP_RET,CS_GRP_INT,CS_GRP_IRET,CS_GRP_PRIVILEGE,CS_GRP_CALL
    from capstone.x86_const import X86_OP_IMM
    for ins in instructions[:-1]:
        if any(ins.group(g) for g in (CS_GRP_JUMP,CS_GRP_RET,CS_GRP_INT,CS_GRP_IRET,CS_GRP_PRIVILEGE)):
            return None,'non-straight-line-gap'
        if ins.mnemonic in ('ud2','hlt','int3','syscall','sysenter'):return None,'trap-in-gap'
        if ins.group(CS_GRP_CALL) and (len(ins.operands)!=1 or ins.operands[0].type!=X86_OP_IMM):
            return None,'indirect-call-in-gap'
    return dict(start=target,end=end,bytes_hex=code.hex(),instructions=len(instructions),
                preceding_functions=sorted(predecessors)),None


def transfer_sites(raw,sections,function,target,decoder):
    start,end=address(function['start']),address(function['end'])
    if not 0<end-start<=65536:return []
    code=read_va(raw,sections,start,end-start)
    if code is None:return []
    instructions=list(decoder.disasm(code,start))
    if sum(x.size for x in instructions)!=len(code):return []
    from capstone import CS_GRP_JUMP,CS_GRP_CALL
    from capstone.x86_const import X86_OP_IMM
    return [dict(site=x.address,mnemonic=x.mnemonic) for x in instructions
            if (x.group(CS_GRP_JUMP) or x.group(CS_GRP_CALL)) and len(x.operands)==1 and
            x.operands[0].type==X86_OP_IMM and x.operands[0].imm&0xffffffff==target]


def audit(a):
    if a.output.exists():raise ValueError('Fresh private report required')
    sys.path.insert(0,str(a.python_deps.resolve()))
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True
    analysis=a.analysis.resolve();paths=generation_paths(analysis,a.generation_name)
    raw,profile,scan,files,_=load_inputs(analysis)
    report=json.loads(paths['report'].read_text())
    if (report['input_profile_sha256']!=sha(analysis/'input-profile.json') or
        report['sdk_sha256']!=scan['sdk_sha256'] or not report['full_selection'] or
        report['gaps']['failed_functions']):raise ValueError('Incomplete or mismatched static generation')
    frozen={paths['report']:sha(paths['report']),analysis/'scan-identity.json':sha(analysis/'scan-identity.json')}
    for name,digest in report['output_files'].items():
        if Path(name).name!=name:raise ValueError('Invalid generated filename')
        p=paths['generated']/name
        if sha(p)!=digest:raise ValueError('Changed generated output')
        frozen[p]=digest
    stubs=paths['generated']/'recomp_stubs_unresolved.c'
    targets=[int(x,16) for x in re.findall(r'recomp_unimpl\("untranslated call target",\s*0x([0-9a-fA-F]+)u\)',stubs.read_text())]
    if len(set(targets))!=len(targets) or len(targets)!=report['gaps']['unresolved_call_targets']:
        raise ValueError('Unresolved target inventory mismatch')
    functions=json.loads(files['functions.json']);by_start={address(f['start']):f for f in functions}
    sections=sections_from_container(raw,profile);candidates={};reasons=Counter()
    for target in sorted(targets):
        finding,reason=assess_gap(raw,sections,functions,target,decoder)
        if finding:candidates[target]=finding
        else:reasons[reason]+=1
    references=defaultdict(list)
    for name in report['output_files']:
        if not re.fullmatch(r'recomp_\d{4}\.c',name):continue
        for ref in source_references((paths['generated']/name).read_text(),set(candidates)):
            symbol=ref['function'] or '';match=re.fullmatch(r'sub_([0-9a-fA-f]{8})',symbol)
            if not match:continue
            owner=by_start.get(int(match[1],16))
            if owner is None:continue
            for site in transfer_sites(raw,sections,owner,ref['target'],decoder):
                item=dict(owner=address(owner['start']),**site)
                if item not in references[ref['target']]:references[ref['target']].append(item)
    accepted=[]
    for at,item in candidates.items():
        if not references[at]:reasons['no-verified-original-direct-transfer']+=1;continue
        accepted.append(dict(item,direct_transfers=references[at]))
    for p in (Path(__file__).resolve(),Path(__file__).with_name('aot_code_data.py'),Path(__file__).with_name('audit_aot_gaps.py')):frozen[p]=sha(p)
    if any(sha(p)!=h for p,h in frozen.items()) or load_inputs(analysis)[:4]!=(raw,profile,scan,files):
        raise ValueError('Inputs changed during audit')
    result=dict(status='STATIC_ENTRY_CANDIDATES_ONLY',unresolved_targets=len(targets),candidate_entries=accepted,
                rejected_reasons=dict(reasons),functions_registered=0,engine_started=False,hardware_tested=False,
                source_sha256={str(p):h for p,h in frozen.items()},
                limits=['Straight-line bounded gaps only','No ABI or original runtime reachability proof',
                        'Does not modify indexes, executable bytes, generated code or runtime traps'])
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('candidate_entries','source_sha256')},indent=2))
    print('candidate_entries:',len(accepted))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('analysis','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--generation-name',required=True);audit(p.parse_args())

"""Conservative private C1 liveness audit, not an AOT-enabling certificate.

Only all-path overwrites before any status export, call, return or uncertain
instruction count as dead. Detailed addresses and paths stay in local output.
"""
import argparse
from bisect import bisect_right
from collections import Counter
import hashlib
import json
from pathlib import Path
import sys


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def analyze(reader,start,decoder,budget=256):
    from capstone import CS_GRP_CALL,CS_GRP_JUMP,CS_GRP_RET,CS_GRP_IRET,CS_GRP_INT
    from capstone.x86_const import X86_OP_IMM
    kills={'fld','fild','fldz','fld1','fldpi','fldl2e','fldl2t','fldlg2','fldln2','fxch'}
    exports={'fnstsw','fstsw','fnstenv','fstenv','fnsave','fsave'}
    pending=[(start,())];ends=[];steps=0
    while pending:
        va,path=pending.pop();steps+=1
        if steps>budget:
            ends.append(dict(address=va,reason='budget'));break
        if va in path:ends.append(dict(address=va,reason='cycle'));continue
        raw=reader(va)
        ins=next(decoder.disasm(raw,va,count=1),None) if raw else None
        if ins is None:ends.append(dict(address=va,reason='unmapped_or_decode'));continue
        m=ins.mnemonic;path=(*path,va)
        if m in kills:ends.append(dict(address=va,reason='overwritten',mnemonic=m));continue
        if m in exports:ends.append(dict(address=va,reason='status_export',mnemonic=m));continue
        if any(ins.group(g) for g in (CS_GRP_CALL,CS_GRP_RET,CS_GRP_IRET,CS_GRP_INT)):
            ends.append(dict(address=va,reason='call_return_or_interrupt',mnemonic=m));continue
        if ins.group(CS_GRP_JUMP):
            if len(ins.operands)!=1 or ins.operands[0].type!=X86_OP_IMM:
                ends.append(dict(address=va,reason='indirect_branch'));continue
            pending.append((ins.operands[0].imm,path))
            if m!='jmp':pending.append((va+ins.size,path))
        elif (m.startswith('f') and m not in ('fnop','fwait')) or m.startswith('xsave') or m in ('ud2','hlt','syscall','sysenter'):
            ends.append(dict(address=va,reason='uncertain_fp_effect',mnemonic=m))
        else:pending.append((va+ins.size,path))
    return dict(status='ALL_PATHS_OVERWRITE_C1' if ends and all(e['reason']=='overwritten' for e in ends) else 'C1_NOT_PROVEN_DEAD',
                steps=steps,ends=ends)


def audit(a):
    if a.output.exists():raise ValueError('Fresh output required')
    proof=json.loads(a.census.read_text());frozen=proof['source_sha256'].copy()
    for p,h in frozen.items():
        if sha(Path(p))!=h:raise ValueError('Stale census dependency '+p)
    frozen[str(Path(__file__).resolve())]=sha(Path(__file__).resolve())
    instructions=a.census.resolve().with_name('instructions.json');frozen[str(instructions)]=sha(instructions)
    sys.path[:0]=[str(a.python_deps.resolve()),str(Path(__file__).resolve().parent)]
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    from audit_aot_gaps import address,sections_from_container
    from aot_code_data import read_va
    analysis=a.analysis.resolve();image=analysis/'fnv-analysis.xbe';profile_path=analysis/'input-profile.json';functions_path=analysis/'disasm/functions.json'
    for p in (image,profile_path,functions_path):
        if frozen.get(str(p))!=sha(p):raise ValueError('Wrong identity-checked analysis')
    profile=json.loads(profile_path.read_text());raw=image.read_bytes();sections=sections_from_container(raw,profile)
    ranges=sorted((address(f['start']),address(f['end'])) for f in json.loads(functions_path.read_text()))
    merged=[]
    for low,high in ranges:
        if merged and low<=merged[-1][1]:merged[-1]=(merged[-1][0],max(high,merged[-1][1]))
        else:merged.append((low,high))
    starts=[r[0] for r in merged]
    def reader(va):
        i=bisect_right(starts,va)-1
        if i<0 or va>=merged[i][1]:return None
        return read_va(raw,sections,va,min(15,merged[i][1]-va))
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True;sites=[]
    for site in json.loads(instructions.read_text()):
        if site['mnemonic'] not in ('f2xm1','fpatan','fyl2x'):continue
        va=site['address'];ins=next(decoder.disasm(reader(va),va,count=1))
        if ins.mnemonic!=site['mnemonic']:raise ValueError('Changed instruction')
        sites.append(dict(address=va,mnemonic=ins.mnemonic,**analyze(reader,va+ins.size,decoder)))
    for p,h in frozen.items():
        if sha(Path(p))!=h:raise ValueError('Changed audit dependency '+p)
    result=dict(status='STATIC_LIVENESS_ONLY',sites=sites,counts=dict(Counter(x['status'] for x in sites)),source_sha256=frozen,
                production_translation_enabled=False,runtime_reachability_proven=False,
                limits=['Only selected decoded ranges are considered','Calls/returns and status exports are barriers','This audit does not validate numeric approximations or enable translation'])
    a.output.mkdir(parents=True);(a.output/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result['counts']))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('analysis','census','python-deps','output'):parser.add_argument('--'+name,type=Path,required=True)
    audit(parser.parse_args())

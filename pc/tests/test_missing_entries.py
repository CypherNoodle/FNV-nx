"""Reject ambiguous entry shapes with synthetic x86 only."""
import argparse
import copy
import json
from pathlib import Path
import sys


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);pc=Path(__file__).resolve().parents[1]
    sys.path[:0]=[str(a.python_deps.resolve()),str(pc/'tools')]
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    from audit_missing_entries import assess_gap,transfer_sites
    from run_aot_analysis import sha
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True
    raw=bytearray(256);raw[:5]=bytes.fromhex('e91b000000');raw[16:32]=b'\x90'*15+b'\xc3'
    raw[32:38]=bytes.fromhex('b801000000c3');raw[38]=0xc3
    sections=[dict(start=0x1000,end=0x1100,offset=0,stored=256,executable=True)]
    functions=[dict(start=0x1000,end=0x1005),dict(start=0x1010,end=0x1020),dict(start=0x1026,end=0x1027)]
    source_before=bytes(raw);function_before=copy.deepcopy(functions)
    finding,reason=assess_gap(raw,sections,functions,0x1020,decoder)
    assert not reason and finding['end']==0x1026 and finding['instructions']==2
    assert transfer_sites(raw,sections,functions[0],0x1020,decoder)==[dict(site=0x1000,mnemonic='jmp')]
    negatives=0
    def reject(code=None,funcs=None,sects=None,target=0x1020):
        nonlocal negatives
        result,reason=assess_gap(raw if code is None else code,sections if sects is None else sects,
                                functions if funcs is None else funcs,target,decoder)
        assert result is None and reason
        negatives+=1
    reject(funcs=functions+[dict(start=0x1020,end=0x1026)])
    reject(funcs=functions[:-1])
    reject(funcs=[*functions[:-1],dict(start=0x1300,end=0x1301)])
    reject(sects=[dict(sections[0],executable=False)])
    reject(sects=[dict(sections[0],stored=33)])
    reject(funcs=[functions[0],dict(start=0x1010,end=0x101f),functions[2]])
    modified=bytearray(raw);modified[31]=0x90;reject(code=modified)
    modified=bytearray(raw);modified[37]=0x90;reject(code=modified)
    for body in ('eb02909090c3','c390909090c3','cc90909090c3','f490909090c3','ffe0909090c3','ffd0909090c3'):
        modified=bytearray(raw);modified[32:38]=bytes.fromhex(body);reject(code=modified)
    # Wrong destination, non-transfer bytes and incomplete owner decoding cannot
    # establish that a generated symbol reference is an original direct branch.
    assert not transfer_sites(raw,sections,functions[0],0x1021,decoder)
    assert not transfer_sites(raw,sections,dict(start=0x1000,end=0x1004),0x1020,decoder)
    modified=bytearray(raw);modified[:5]=b'\x90'*5
    assert not transfer_sites(modified,sections,functions[0],0x1020,decoder)
    assert bytes(raw)==source_before and functions==function_before
    paths=[Path(__file__).resolve(),pc/'tools/audit_missing_entries.py',pc/'tools/aot_code_data.py',pc/'tools/audit_aot_gaps.py']
    result=dict(status='PASS',positive_gap_cases=1,verified_direct_transfers=1,rejected_gap_cases=negatives,
                rejected_transfer_cases=3,input_state_unchanged=True,game_code_used=False,
                source_sha256={str(p):sha(p) for p in paths})
    (a.output/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    test(p.parse_args())

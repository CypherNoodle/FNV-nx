"""Synthetic control-flow boundaries for explicitly observed missing entries."""
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_gap_function import audit

def test():
    # Two returns beyond a conditional branch; embedded unreachable INT3 is not code.
    code=bytes.fromhex('85c07404b001c3cc31c0c3')
    r=audit(code,0x1000,0x1000,0x1000+len(code),[],bytes.fromhex('0010000000100000'))
    assert r['returns']==[0x1006,0x100a] and r['end']==0x100b and r['aligned_reference_file_offsets']==[0,4]
    assert audit(bytes.fromhex('e90b000000'),0x1000,0x1000,0x1005,[dict(start=0x1010,end=0x1020)])['tail_exits']==[0x1010]
    rejected=0
    cases=[(bytes.fromhex('ebff'),[],2), # jump into its own second byte
           (bytes.fromhex('eb7f'),[],2), # external branch without an audited entry
           (bytes.fromhex('ffe0'),[],2), # computed jump
           (bytes.fromhex('cc'),[],1),
           (bytes.fromhex('0f'),[],1),
           (bytes.fromhex('90'),[],1), # fallthrough outside gap
           (bytes.fromhex('90c3'),[dict(start=0x1001,end=0x1002)],2),
           (bytes.fromhex('c3'),[dict(start=0x1000,end=0x1001)],1)]
    for code,existing,size in cases:
        try:audit(code,0x1000,0x1000,0x1000+size,existing)
        except ValueError:rejected+=1
        else:raise AssertionError('Unsafe gap accepted')
    assert rejected==8
    print('PASS gap CFG, return closure, reference alignment and eight rejections')

if __name__=='__main__':test()

"""Synthetic positive and conflicting evidence for indexed pointer tables."""
import argparse
from copy import deepcopy
from pathlib import Path
import struct
import sys

def test(deps):
    sys.path[:0]=[str(Path(__file__).resolve().parents[1]/'tools'),str(deps.resolve())]
    import capstone
    from audit_orphan_jump_tables import find_tables
    base=0x401000;seed=base+0x200;table=seed+2
    raw=bytearray(b'\xcc'*0x400)
    raw[:10]=bytes.fromhex('558becff2485')+struct.pack('<I',table)
    for at in (0x20,0x30,0x40):raw[at:at+6]=b'\xb8'+struct.pack('<I',at)+b'\xc3'
    raw[0x200:0x202]=bytes.fromhex('8bff')
    struct.pack_into('<III',raw,0x202,base+0x20,base+0x30,base+0x40)
    section=dict(start=base,end=base+len(raw),offset=0,stored=len(raw),executable=True)
    owner=dict(start=hex(base),end=hex(base+0x80),detection_method='prologue',has_prologue=True,called_by=[])
    fake=dict(start=hex(seed),end=hex(seed+18),detection_method='gap_prologue',has_prologue=False,called_by=[],calls_to=[])
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
    def audit(blob=raw,functions=None,refs=None):return find_tables(bytes(blob),[section],functions or [owner,fake],refs or [],decoder)
    before=(bytes(raw),deepcopy(owner),deepcopy(fake));good=audit()
    assert len(good)==1 and good[0]['entries']==3 and good[0]['dispatches']==[dict(site=base+3,owner=base)]
    assert (bytes(raw),owner,fake)==before
    checks=1
    def reject(blob=raw,functions=None,refs=None):
        nonlocal checks
        assert not audit(blob,functions,refs);checks+=1
    # Wrong scaling, an unreachable dispatch, a missing real instruction,
    # and a matching byte sequence inside another instruction all fail.
    bad=bytearray(raw);bad[5]=0x45;reject(bad)
    bad=bytearray(raw);bad[0]=0xc3;reject(bad)
    bad=bytearray(raw);bad[3:6]=b'\x90'*3;reject(bad)
    bad=bytearray(raw);bad[:3]=b'\xb8\x00\x00';reject(bad)
    bad=bytearray(raw);struct.pack_into('<I',bad,0x202,base+0x21);reject(bad)
    bad=bytearray(raw);struct.pack_into('<I',bad,0x202,0x800000);reject(bad)
    bad=bytearray(raw);bad[0x20e]=0x55;reject(bad)
    for kind,target in (('call',seed),('data_read',table),('jump',seed+1)):
        reject(refs=[dict(type=kind,**{'from':hex(base+0x50),'to':hex(target)})])
    called=deepcopy(fake);called['called_by']=[hex(base)];reject(functions=[owner,called])
    weak=deepcopy(owner);weak['detection_method']='gap_prologue';weak['has_prologue']=False
    reject(functions=[weak,fake])
    protected=deepcopy(owner);protected['start']=hex(table);protected['end']=hex(table+1)
    reject(functions=[owner,fake,protected])
    print('PASS:',checks,'synthetic table/dispatch checks; original inputs unchanged')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--python-deps',type=Path,required=True)
    test(p.parse_args().python_deps)

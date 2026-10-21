"""Constructed unaligned table evidence; malformed dispatches must reject."""
import argparse
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_crt_math_table import inspect_table


def test(deps):
    sys.path.insert(0,str(deps.resolve()));from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    base=0x400000;setup=base+0x100;route=base+0x200;dispatch=base+0x300;selector=base+0x500;descriptor=base+0x602
    raw=bytearray(4096)
    def put(at,data):raw[at-base:at-base+len(data)]=data
    def rel(at,op,target):return bytes((op,))+struct.pack('<i',target-at-5)
    put(setup,b'\xba'+struct.pack('<I',descriptor)+rel(setup+5,0xe9,route))
    put(route,rel(route,0xe8,dispatch)+b'\xc3')
    code=b'\xbb'+struct.pack('<I',selector)+bytes.fromhex('240fd70fbec081e10404000089d301c383c310ff23')
    put(dispatch,code);put(selector,bytes((0,4,8,12)*4));targets=[base+0x802+i*16 for i in range(4)]
    put(descriptor+16,struct.pack('<4I',*targets))
    functions={setup:dict(end=setup+10),route:dict(end=route+6),dispatch:dict(end=dispatch+len(code))}
    for at in targets:put(at-2,bytes.fromhex('9090ddd8d9e8c3'));functions[at-2]=dict(end=at+5)
    def read(at,size):
        if at<base or at+size>base+len(raw):raise ValueError('Out of bounds')
        return bytes(raw[at-base:at-base+size])
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);checks=0
    def call():return inspect_table(decoder,read,functions,setup,dispatch)
    result=call();assert result['unaligned_pointer_table'] and [r['target'] for r in result['entries']]==targets
    assert all(r['existing_linear_boundary'] and not r['already_indexed'] for r in result['entries']);checks+=1
    assert result['indexed_route']==[setup,route,dispatch] and not result['registered_entries'];checks+=1
    original=bytes(raw)
    def reject():
        nonlocal checks
        try:call()
        except ValueError:checks+=1
        else:raise AssertionError('Malformed dispatch accepted')
    for at,value in [(setup,0xb9),(setup+5,0xff),(selector,16),(dispatch+len(code)-1,0x20),
                     (dispatch+1,0xff),(route,0xc3)]:
        raw[:]=original;raw[at-base]=value;reject()
    raw[:]=original;put(descriptor+16,struct.pack('<I',0xffffffff));reject()
    raw[:]=original;f=functions.pop(setup);reject();functions[setup]=f
    f=functions.pop(dispatch);reject();functions[dispatch]=f
    assert call()['table_sha256']==result['table_sha256'];checks+=1
    # Recover a shared terminal tail reached by a direct original jump.
    raw[:]=original;shared=base+0x900;put(targets[0],rel(targets[0],0xe9,shared));put(shared,bytes.fromhex('d9e8c3'))
    closure=call()['recovered_cfg_entries'];assert shared in [r['start'] for r in closure];checks+=1
    raw[:]=original;put(targets[0],rel(targets[0],0xe8,shared));reject()
    raw[:]=original;put(targets[0],bytes.fromhex('ffe0'));reject()
    raw[:]=original;put(descriptor+16,struct.pack('<I',targets[0]+1));reject()
    raw[:]=original;put(targets[0],bytes.fromhex('eb029090c3'));reject()
    raw[:]=original;put(targets[0],bytes.fromhex('ebfe'));reject()
    raw[:]=original;assert len(call()['recovered_cfg_entries'])==4
    print('PASS',checks,'synthetic unaligned table/evidence rejection checks')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--python-deps',type=Path,required=True);test(p.parse_args().python_deps)

"""Synthetic checks for exact initializer coverage and preserved caller state."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
from pe_fixture import write_pe


def test(a):
    if a.output.exists():raise ValueError('Use a fresh output')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    sys.path.insert(0,str(pc/'tools'))
    from initializer_trace import table_entries,check_trace
    from run_aot_analysis import sha
    inputs=[Path(__file__).resolve(),pc/'tests/pe_fixture.py',pc/'tools/initializer_trace.py',pc/'tools/audit_install.py']
    frozen={p:sha(p) for p in inputs}
    exe=out/'fixture.exe';write_pe(exe,section='.rdata');raw=bytearray(exe.read_bytes())
    values=[0x401050,0,0x401050,0x401070];data=struct.pack('<4I',*values)
    raw[0x200:0x210]=data;exe.write_bytes(raw)
    table=dict(identity=dict(executable_sha256=sha(exe)),start=0x401000,end=0x401010,
               slots=4,nonzero=3,unique_roots=2,roots=[0x401050,0x401070],
               table_sha256=hashlib.sha256(data).hexdigest())
    entries=table_entries(exe,table)
    before=[[cursor,table['end'],target,0x10000,0x10000,0x20000,0x30000] for cursor,target in entries]
    after=[[r[0],r[1],r[3],r[4],r[5]+4,r[6]] for r in before]
    accepted=check_trace(table,entries,before,after)
    if not accepted['complete'] or accepted['expected_nonzero_slots']!=3:raise AssertionError('Repeated initializer callback lost')
    negative=0
    def reject_trace(b,e):
        nonlocal negative
        try:r=check_trace(table,entries,b,e)
        except ValueError:negative+=1;return
        if r['complete']:raise AssertionError('Incomplete/wrong initializer traversal accepted')
        negative+=1
    reject_trace(before[:2],after[:2]) # Unique target count does not prove slot coverage.
    reject_trace(before,after[:-1]);reject_trace(before+[before[-1]],after+[after[-1]])
    reject_trace(list(reversed(before)),list(reversed(after)))
    for column,value in [(0,0x401004),(1,0x401014),(2,0x401074)]:
        b=copy.deepcopy(before);b[1][column]=value;reject_trace(b,after)
    for column in range(6):
        e=copy.deepcopy(after);e[1][column]+=4;reject_trace(before,e)
    b=copy.deepcopy(before);b[1][0]=True;reject_trace(b,after)
    b=copy.deepcopy(before);b[1][0]=-1;reject_trace(b,after)
    reject_trace(None,after);reject_trace(before,[[]])
    def reject_table(change):
        nonlocal negative
        t=copy.deepcopy(table);t.update(change)
        try:table_entries(exe,t)
        except ValueError:negative+=1;return
        raise AssertionError('Mismatched initializer table accepted')
    for change in [dict(table_sha256='0'*64),dict(identity=dict(executable_sha256='0'*64)),
                   dict(slots=3),dict(nonzero=2),dict(unique_roots=3),dict(roots=[0x401050]),
                   dict(start=0x401001),dict(end=0x401000),dict(end=0x402000)]:reject_table(change)
    if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Test sources changed')
    result=dict(status='PASS',nonzero_slots=3,unique_callbacks=2,repeated_callback_preserved=True,
                negative_checks=negative,source_sha256={str(p):h for p,h in frozen.items()},
                game_code_executed=False,nro_built=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    test(p.parse_args())

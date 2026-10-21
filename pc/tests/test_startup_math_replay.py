"""Real native cache parser/hit/miss checks with an actual target-core result."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from replay_startup_math import encode
from x87_target_evaluator import Evaluator,sha,unchanged


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    core=json.loads(a.core.read_text());unchanged(core['source_sha256']);cc=core['compiler']
    if core['switch_abi'] or core['transcendental_model'] is not None or len(core['objects'])!=99:raise ValueError('Strict host arithmetic core required')
    native=pc/'tests/startup_math_replay_host.c';bridge=pc/'tests/startup_math_stop_host.c'
    sources=[native,bridge];rsp=out/'link.rsp';program=out/'test.exe'
    objects=[Path(i['object']) for i in core['objects']]
    for item in core['objects']:
        if sha(Path(item['object']))!=item['sha256']:raise ValueError('Stale native arithmetic object')
    includes=['-I'+str(p) for p in (pc/'native',pc/'tests',a.sdk/'templates/runtime',a.sdk/'src/kernel')]
    args=[*cc,'-std=c11','-O1','-DFNV_CPU_EXTENDED_X87=1','-DFNV_CPU_X87_QUAD_MATH=1',*includes,*map(str,sources),*map(str,objects),'-o',str(program)]
    # Keep the command below the Windows argument limit by using a response file.
    rsp.write_text('\n'.join('"'+x.replace('\\','/')+'"' for x in args[len(cc):])+'\n')
    r=subprocess.run([*cc,'@'+str(rsp)],capture_output=True,text=True);(out/'compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr)
    one=struct.pack('<QH',1<<63,0x3fff);state=bytes(10)+one*7+struct.pack('<HHBB',0x37f,0,0,3)
    request=bytes((0,0))+state;target=Evaluator(a.evaluator,a.python_deps);result,after=target.evaluate(request);assert result==0
    good=encode([(request,result,after)]);header=good[:16];record=good[16:];checks=0
    cases={'valid':(good,'hit'),'short':(good[:7],'reject'),'magic':(b'BADMAGIC'+good[8:],'reject'),
           'reserved':(good[:12]+struct.pack('<I',1)+good[16:],'reject'),
           'capacity':(good[:8]+struct.pack('<I',4097)+good[12:],'reject'),
           'truncated':(good[:-1],'reject'),'trailing':(good+b'X','reject'),
           'duplicate':(good[:8]+struct.pack('<I',2)+good[12:]+record,'reject'),
           'kind':(header+bytes((2,))+record[1:],'reject'),
           'op':(header+record[:1]+bytes((3,))+record[2:],'reject'),
           'result':(header+record[:88]+struct.pack('<I',5)+record[92:],'reject'),
           'error-mutation':(header+record[:88]+struct.pack('<I',4)+bytes((record[92]^1,))+record[93:],'reject')}
    trig_request=bytes((1,0))+state;trig_result,trig_after=target.evaluate(trig_request);assert trig_result==0
    cases['threaded']=(encode([(request,result,after),(trig_request,trig_result,trig_after)]),'threads')
    for name,(data,mode) in cases.items():
        p=out/(name+'.bin');p.write_bytes(data);r=subprocess.run([program,p,mode],capture_output=True,text=True,timeout=15)
        (out/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise AssertionError(name+' failed: '+r.stdout+r.stderr)
        checks+=1
    report=dict(status='PASS',native_cases=checks,exact_target_answer=True,cache_miss_stops=True,
                game_inputs=False,hardware_tested=False,
                source_sha256={str(p.resolve()):sha(p) for p in [Path(__file__),native,bridge,pc/'tests/startup_math_state.h',a.core,a.evaluator]})
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS',checks,'native replay cache/hit/miss cases')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('core','sdk','evaluator','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    test(p.parse_args())

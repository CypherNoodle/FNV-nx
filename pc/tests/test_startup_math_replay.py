"""Real native cache parser/hit/miss checks with an actual target-core result."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from replay_startup_math import encode,decode_cache,MAX_REPLAY_RECORDS
from x87_target_evaluator import Evaluator,sha,unchanged


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    core=json.loads(a.core.read_text());unchanged(core['source_sha256']);cc=a.cc or core['compiler']
    if core['switch_abi'] or core['transcendental_model'] is not None or len(core['objects'])!=99:raise ValueError('Strict host arithmetic core required')
    native=pc/'tests/startup_math_replay_host.c';bridge=pc/'tests/startup_math_stop_host.c'
    sources=[native,bridge];rsp=out/'link.rsp';program=out/'test.exe'
    objects=[Path(i['object']) for i in core['objects']]
    frozen={str(p.resolve()):sha(p) for p in [Path(__file__),native,bridge,pc/'tests/startup_math_state.h',a.core,a.evaluator,Path(cc[0]),*objects]}
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
           'capacity':(good[:8]+struct.pack('<I',MAX_REPLAY_RECORDS+1)+good[12:],'reject'),
           'truncated':(good[:-1],'reject'),'trailing':(good+b'X','reject'),
           'duplicate':(good[:8]+struct.pack('<I',2)+good[12:]+record,'reject'),
           'kind':(header+bytes((2,))+record[1:],'reject'),
           'op':(header+record[:1]+bytes((3,))+record[2:],'reject'),
           'result':(header+record[:88]+struct.pack('<I',5)+record[92:],'reject'),
           'error-mutation':(header+record[:88]+struct.pack('<I',4)+bytes((record[92]^1,))+record[93:],'reject')}
    trig_request=bytes((1,0))+state;trig_result,trig_after=target.evaluate(trig_request);assert trig_result==0
    cases['threaded']=(encode([(request,result,after),(trig_request,trig_result,trig_after)]),'threads')
    # Parser-capacity fixtures: unique, structurally valid rows. Only the first
    # row is executed; these extra rows are not numerical evaluator evidence.
    boundary_rows=[(request,result,after)]
    for n in range(1,MAX_REPLAY_RECORDS):
        q=bytearray(request);output=bytearray(after)
        q[72:80]=struct.pack('<Q',n);output[70:78]=struct.pack('<Q',n)
        boundary_rows.append((bytes(q),result,bytes(output)))
    for count in (4097,MAX_REPLAY_RECORDS):
        blob=encode(boundary_rows[:count]);assert len(decode_cache(blob))==count
        cases['capacity-valid-'+str(count)]=(blob,'hit')
    for bad in (good[:8]+struct.pack('<I',MAX_REPLAY_RECORDS+1)+good[12:],good[:-1],good+b'X'):
        try:decode_cache(bad)
        except ValueError:pass
        else:raise AssertionError('Python cache decoder accepted malformed boundary')
    try:encode(boundary_rows+[(request,result,after)])
    except ValueError:pass
    else:raise AssertionError('Python cache encoder accepted over-capacity input')
    quarter=struct.pack('<QH',1<<63,0x3ffd);seed_state=bytearray(state);seed_state[:10]=quarter
    delta_rows=[];variants=[]
    for op in (0,1,3):
        q=bytes((1,op))+seed_state;r,output=target.evaluate(q);assert r==0;delta_rows.append((q,r,output))
        if op==3:continue
        for k in range(8):
            changed=bytearray(seed_state)
            for j in range(2,8):changed[j*10:j*10+10]=bytes((k*37+j*19+n*53)&255 for n in range(10))
            wire=bytes((1,op))+changed;r,answer=target.evaluate(wire);assert r==0;variants.append((wire,r,answer))
    variants_file=out/'empty-variants.bin';variants_file.write_bytes(encode(variants))
    delta_cache=encode([(request,result,after),*delta_rows])
    cases['empty-delta']=(delta_cache,'delta');cases['empty-delta-disabled']=(delta_cache,'delta-disabled')
    q,r,answer=delta_rows[0];modified=bytearray(q);modified[22]^=1;bad_answer=bytearray(answer);bad_answer[20]^=1
    cases['unsafe-delta']=(encode([(request,result,after),(q,r,bytes(bad_answer))]),'delta-unsafe')
    conflicting=bytearray(bad_answer);conflicting[:20]=answer[:20];conflicting[0]^=1
    cases['conflicting-delta']=(encode([(q,r,answer),(bytes(modified),r,bytes(conflicting))]),'delta-reject')
    for name,(data,mode) in cases.items():
        p=out/(name+'.bin');p.write_bytes(data);command=[program,p,mode]
        if mode in ('delta','delta-disabled','delta-unsafe'):command.append(variants_file)
        r=subprocess.run(command,capture_output=True,text=True,timeout=15)
        (out/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise AssertionError(name+' failed: '+r.stdout+r.stderr)
        checks+=1
    unchanged(frozen)
    report=dict(status='PASS',native_cases=checks,empty_register_target_comparisons=len(variants),exact_target_answer=True,cache_miss_stops=True,
                game_inputs=False,hardware_tested=False,
                source_sha256=frozen)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS',checks,'native replay cache/hit/miss cases')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('core','sdk','evaluator','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',help='Optional compatible host compiler; core objects remain hash-checked')
    test(p.parse_args())

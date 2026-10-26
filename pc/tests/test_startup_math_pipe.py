"""Real native pipe serialization, exact target replies and malformed-response checks."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from x87_target_evaluator import Evaluator,sha,unchanged
from run_target_math_session import session

def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True);pc=Path(__file__).resolve().parents[1]
    sources=[pc/'tests'/n for n in ('startup_math_pipe.c','startup_math_pipe.h','startup_math_state.h','startup_math_pipe_host.c')]
    sources.extend([Path(__file__).resolve(),pc/'tools/run_target_math_session.py',pc/'native/host_mutex.h',pc/'native/x87_stack.h'])
    core=json.loads(a.core.read_text());unchanged(core['source_sha256'])
    if core['switch_abi'] or core['transcendental_model'] is not None or len(core['objects'])!=99:raise ValueError('Strict host core required')
    objects=[Path(i['object']) for i in core['objects']]
    for item in core['objects']:
        if sha(Path(item['object']))!=item['sha256']:raise ValueError('Changed arithmetic object')
    frozen={str(p.resolve()):sha(p) for p in [*sources,a.cc,a.core,*objects]};program=out/'test.exe'
    command=[str(a.cc),'-std=c11','-O1','-Wall','-Wextra','-Werror','-I'+str(pc/'tests'),'-I'+str(pc/'native'),str(sources[0]),str(pc/'tests/startup_math_pipe_host.c'),'-o',str(program)]
    env=os.environ.copy();env['PATH']=str(a.cc.parent)+os.pathsep+env['PATH']
    command[-2:-2]=list(map(str,objects));rsp=out/'link.rsp';rsp.write_text('\n'.join('"'+x.replace('\\','/')+'"' for x in command[1:])+'\n')
    r=subprocess.run([command[0],'@'+str(rsp)],capture_output=True,text=True,env=env);(out/'compiler.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
    target=Evaluator(a.evaluator,a.python_deps)
    r=session([program,'threaded'],target,out/'threaded',timeout=30,env=env)
    assert r['requests']==14 and r['exit_code']==0 and r['host']['status']=='PASS'
    applied=[bytes.fromhex(line.split()[1]) for line in (out/'threaded/stdout.log').read_text().splitlines() if line.startswith('FNV_X87_APPLIED ')]
    assert len(applied)==1792
    verified={}
    for row in applied:
        if row[:88] not in verified:verified[row[:88]]=target.evaluate(row[:88])
        assert verified[row[:88]]==(struct.unpack_from('<I',row,88)[0],row[92:])
    assert len(verified)==14
    memo=session([program,'memo'],target,out/'memo',timeout=30,env=env)
    assert memo['requests']==5 and memo['exit_code']==0 and memo['host']['status']=='PASS'
    memo_applied=[bytes.fromhex(line.split()[1]) for line in (out/'memo/stdout.log').read_text().splitlines() if line.startswith('FNV_X87_APPLIED ')]
    assert len(memo_applied)==8 and memo_applied[0][:88]!=memo_applied[2][:88]
    for row in memo_applied:assert target.evaluate(row[:88])==(struct.unpack_from('<I',row,88)[0],row[92:])
    for mode in ('eof','echo','hex','size','result','error-mutation','top'):
        child=subprocess.Popen([str(program),'negative'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env)
        request=child.stdout.readline();assert request.startswith(b'FNV_X87_REQUEST ');wire=bytes.fromhex(request.decode().split()[1]);result,state=target.evaluate(wire);row=bytearray(wire+struct.pack('<I',result)+state)
        if mode=='echo':row[0]^=1
        if mode=='result':row[88]=5
        if mode=='error-mutation':row[88]=1;row[92]^=1
        if mode=='top':row[176]=8
        reply=b'FNV_X87_REPLY '+row.hex().encode()+b'\n'
        if mode=='hex':reply=reply[:14]+b'zz'+reply[16:]
        if mode=='size':reply=reply[:-3]+b'\n'
        stdout,stderr=child.communicate(b'' if mode=='eof' else reply,timeout=10)
        (out/(mode+'.log')).write_bytes(request+stdout+stderr);assert child.returncode==0 and b'"PASS"' in stdout,(mode,stderr)
    child=subprocess.Popen([str(program),'cached-negative'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env=env)
    first=child.stdout.readline();wire=bytes.fromhex(first.decode().split()[1]);result,state=target.evaluate(wire)
    child.stdin.write(b'FNV_X87_REPLY '+(wire+struct.pack('<I',result)+state).hex().encode()+b'\n');child.stdin.flush()
    applied_line=child.stdout.readline();second=child.stdout.readline()
    assert applied_line.startswith(b'FNV_X87_APPLIED ') and second.startswith(b'FNV_X87_REQUEST ') and first!=second
    stdout,stderr=child.communicate(b'broken\n',timeout=10)
    (out/'cached-negative.log').write_bytes(first+applied_line+second+stdout+stderr)
    assert child.returncode==0 and b'"PASS"' in stdout and b'FNV_X87_REQUEST' not in stdout
    unchanged(frozen)
    (out/'validation.json').write_text(json.dumps(dict(status='PASS',target_requests=14,exact_applications=len(applied),collision_reopen_and_control_checks=8,cached_channel_failure_checked=True,malformed_or_missing_replies=7,source_sha256=frozen,engine_started=False,hardware_tested=False),indent=2)+'\n')
    print('PASS 1792 concurrent exact applications, collision/reopen/control checks and 8 fail-closed cases')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('cc','core','evaluator','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    test(p.parse_args())

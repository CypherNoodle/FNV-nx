"""Bounded desktop diagnostic session with exact ARM64 math replies.

All logs, states, caches and reports are private. This is neither a CPU backend
nor evidence of game boot or hardware performance. The child must explicitly
enable the diagnostic pipe and reserve stdin/stdout for this protocol.
"""
import argparse
from collections import OrderedDict
import json
from pathlib import Path
import queue
import subprocess
import threading
import time
from replay_startup_math import encode,MAX_REPLAY_RECORDS
from x87_target_evaluator import Evaluator, sha, unchanged
MAX_SESSION_REQUESTS=65536


def session(command, target, output, max_requests=4096, timeout=180, env=None):
    output=Path(output).resolve()
    if output.exists() or not command or not 1<=max_requests<=MAX_SESSION_REQUESTS or not 1<=timeout<=600:
        raise ValueError('Fresh output, executable and bounded budgets required')
    command=list(map(str,command));program=Path(command[0]).resolve()
    frozen={str(program):sha(program),str(Path(__file__).resolve()):sha(Path(__file__)),
            str(target.path):sha(target.path),**target.report['source_sha256']}
    for name,digest in target.report['generated_sha256'].items():frozen[str(target.path.parent/name)]=digest
    unchanged(frozen);output.mkdir(parents=True);messages=queue.Queue(maxsize=64)
    records=OrderedDict();requests=0;evaluations=0;evictions=0;host=None;reason='CHILD_EXIT';deadline=time.monotonic()+timeout
    with (output/'stderr.log').open('wb') as err,(output/'stdout.log').open('wb') as log:
        child=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=err,env=env)
        def reader():
            try:
                while True:
                    line=child.stdout.readline(8193)
                    messages.put(line)
                    if not line or len(line)>8192:return
            except Exception as ex:messages.put(ex)
        worker=threading.Thread(target=reader,daemon=True);worker.start()
        try:
            while True:
                remaining=deadline-time.monotonic()
                if remaining<=0:reason='TIME_LIMIT';break
                try:line=messages.get(timeout=min(remaining,1))
                except queue.Empty:continue
                if isinstance(line,Exception):raise line
                if not line:break
                if len(line)>8192:raise ValueError('Oversized diagnostic line')
                log.write(line);log.flush();text=line.decode('ascii',errors='replace').strip()
                if text.startswith('FNV_X87_REQUEST '):
                    if requests==max_requests:
                        reason='REQUEST_LIMIT'
                        if not child.stdin.closed:child.stdin.close()
                        continue
                    wire=text[len('FNV_X87_REQUEST '):]
                    if len(wire)!=176 or any(c not in '0123456789abcdef' for c in wire):raise ValueError('Malformed request')
                    request=bytes.fromhex(wire)
                    if request[0]>1 or request[1]>(3 if request[0] else 2) or request[86]>7:raise ValueError('Invalid request state')
                    hit=request in records
                    if hit:result,state=records[request]
                    else:result,state=target.evaluate(request);evaluations+=1
                    # encode validates the exact result range and failure-state immutability.
                    row=encode([(request,result,state)])[16:]
                    if not hit:
                        if len(records)==MAX_REPLAY_RECORDS:records.popitem(last=False);evictions+=1
                        records[request]=(result,state)
                    records.move_to_end(request)
                    child.stdin.write(b'FNV_X87_REPLY '+row.hex().encode()+b'\n');child.stdin.flush();requests+=1
                    if requests%64==0:print('Exact target math requests:',requests,flush=True)
                elif text.startswith('{'):
                    try:host=json.loads(text)
                    except json.JSONDecodeError:pass
            if reason=='TIME_LIMIT':child.kill()
            child.wait(timeout=5)
        finally:
            if child.poll() is None:child.kill();child.wait()
            if not child.stdin.closed:child.stdin.close()
            child.stdout.close();worker.join(timeout=2)
    unchanged(frozen)
    cache=output/'answers.bin';cache.write_bytes(encode([(k,*v) for k,v in records.items()]))
    report=dict(status='TARGET_MATH_SESSION_STOPPED',reason=reason,exit_code=child.returncode,
                requests=requests,unique_requests=None if evictions else len(records),
                retained_requests=len(records),evaluations=evaluations,cache_evictions=evictions,
                cache_capacity=MAX_REPLAY_RECORDS,host=host,source_sha256=frozen,
                answer_cache_sha256=sha(cache),numerical_model=target.report['model'],
                engine_started=False,hardware_tested=False)
    (output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('evaluator','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--max-requests',type=int,default=4096);p.add_argument('--timeout',type=int,default=180)
    p.add_argument('command',nargs=argparse.REMAINDER);a=p.parse_args()
    command=a.command[1:] if a.command[:1]==['--'] else a.command
    r=session(command,Evaluator(a.evaluator,a.python_deps),a.output,a.max_requests,a.timeout)
    print(json.dumps({k:v for k,v in r.items() if k!='source_sha256'},indent=2))

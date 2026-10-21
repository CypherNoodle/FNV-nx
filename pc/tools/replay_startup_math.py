"""Bounded offline replay of exact ARM64 math in a frozen host CRT image.

Each cache miss stops the host process. Evaluate only that exact state on
the verified target core, write a new immutable cache, and restart the same
image. This establishes diagnostic control flow, never game performance,
cold boot, or Switch hardware behavior. All observations stay private.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
from startup_memory_profile import configuration
from x87_target_evaluator import Evaluator,sha,unchanged


def encode(records):
    if len(records)>4096:raise ValueError('Replay record limit')
    keys=set();data=[]
    for request,result,state in records:
        if (len(request)!=88 or len(state)!=86 or request[0]>1 or
            request[1]>(3 if request[0] else 2) or not 0<=result<=4 or
            (result and state!=request[2:]) or request in keys):
            raise ValueError('Invalid or duplicate replay record')
        keys.add(request);data.append(request+struct.pack('<I',result)+state)
    return b'FNVX87R1'+struct.pack('<II',len(records),0)+b''.join(data)


def replay(a):
    if a.output.exists() or not 1<=a.max_records<=4096:raise ValueError('Fresh output and bounded record count required')
    baseline=a.baseline.resolve();d=json.loads(baseline.read_text());host=d['host']
    if (d.get('status')!='EXPERIMENT_STOPPED' or not d.get('all_generated') or
        d.get('rejected_functions') or d.get('host_x87',{}).get('target_math_behavior')!='explicit-diagnostic-stop' or
        host.get('math_replay',{}).get('schema')!='fnv-x87-exact-replay-v1' or
        host['math_replay'].get('hits')!=0 or not host.get('math_request')):
        raise ValueError('Require a frozen complete uncached math-stop baseline')
    if sha(a.exe)!=d['identity']['executable_sha256']:raise ValueError('Different EXE')
    frozen={**d['source_sha256'],str(baseline):sha(baseline),str(a.exe.resolve()):sha(a.exe)}
    for name,digest in d['generated_files_sha256'].items():
        if Path(name).name!=name:raise ValueError('Invalid diagnostic filename')
        frozen[str(baseline.parent/name)]=digest
    program=baseline.parent/'startup-test.exe';frozen[str(program)]=d['executable_sha256']
    for item in d['compiled']:frozen[str(baseline.parent/item['object'])]=item['object_sha256']
    frozen[str(Path(__file__).resolve())]=sha(Path(__file__))
    unchanged(frozen)
    target=Evaluator(a.evaluator,a.python_deps)
    target_sources=dict(target.report['source_sha256']);target_sources[str(target.path)]=sha(target.path)
    for name,digest in target.report['generated_sha256'].items():target_sources[str(target.path.parent/name)]=digest
    model=d['host_x87']['profile']['transcendental_model']
    if target.report['model']!=model:raise ValueError('Wrong numerical model')
    a.output.mkdir(parents=True);records=[];observations=[];reason='RECORD_LIMIT'
    memory=configuration(d['memory_profile'])
    for step in range(a.max_records):
        request=host.get('math_request')
        if not request or not host['text'].startswith('Target ARM64 x87 '):
            reason='NEXT_DIAGNOSTIC_STOP';break
        wire=bytes((request['kind'],request['operation']))+bytes.fromhex(request['state_hex'])
        if any(k==wire for k,_,_ in records):raise ValueError('Host stopped on an already replayed exact state')
        result,state=target.evaluate(wire)
        if result:
            reason='TARGET_REJECTED_REQUEST';observations.append(dict(target_result=result,request=wire.hex()));break
        records.append((wire,result,state));cache=a.output/f'cache-{step+1:04d}.bin'
        cache.write_bytes(encode(records));frozen[str(cache.resolve())]=sha(cache)
        args=[program,a.exe.resolve(),a.exe.resolve().parent]
        if d['text_data']:args.append(baseline.parent/'captured-text-data.bin')
        args.extend(str(memory[k]) for k in ('arena_bytes','vm_bytes','commit_limit'));args.append(cache.resolve());args.append(str(d['host'].get('worker_stats',{}).get('capacity',0)))
        run=subprocess.run([str(v) for v in args],capture_output=True,text=True,timeout=120)
        log=a.output/f'host-{step+1:04d}.log';log.write_text(run.stdout+run.stderr)
        if run.returncode:raise RuntimeError('Host replay failed: '+str(log))
        host=json.loads(run.stdout.strip().splitlines()[-1]);counts=host.get('math_replay',{})
        if counts.get('hits',0)<1:raise ValueError('Replay produced no verified cache hits')
        observation=dict(step=step+1,cache_sha256=sha(cache),host=host)
        observations.append(observation);(a.output/f'observation-{step+1:04d}.json').write_text(json.dumps(observation,indent=2)+'\n')
        print('replay',step+1,'hits',counts['hits'],'calls',host['call_count'],'stop',host['text'],flush=True)
        if not host.get('math_request') or not host['text'].startswith('Target ARM64 x87 '):reason='NEXT_DIAGNOSTIC_STOP';break
    unchanged(frozen);unchanged(target_sources)
    report=dict(status='EXACT_TARGET_REPLAY_STOPPED',reason=reason,records=len(records),
                host=host,observations=observations,numerical_model=model,memory_profile=d['memory_profile'],
                source_sha256={**frozen,**target_sources},baseline_sha256=sha(baseline),
                engine_started=False,cold_boot_supported=False,switch_hardware_tested=False,
                suitable_for_switch_packaging=False)
    (a.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(reason,'records',len(records),'calls',host['call_count'],flush=True)
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('baseline','evaluator','exe','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--max-records',type=int,default=128);replay(p.parse_args())

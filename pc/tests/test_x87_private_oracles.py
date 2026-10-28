"""Run existing real-x87 host oracles against the private-state core.

Inputs are retained synthetic request streams, not game data. Each host helper
compares the software result to actual x87 instructions in this execution.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True);pc=Path(__file__).resolve().parents[1]
    proof=json.loads(a.native.read_text())
    if not proof.get('thread_local_state'):raise ValueError('Require private state')
    for n,h in proof['source_sha256'].items():
        if sha(n)!=h:raise ValueError('Changed core source')
    for r in proof['objects']:
        if sha(r['object'])!=r['sha256']:raise ValueError('Changed core object')
    rsp=out/'objects.rsp';rsp.write_text('\n'.join('"'+str(Path(r['object'])).replace('\\','/')+'"' for r in proof['objects'])+'\n')
    cases=[('control','x87_control_host.c',a.control_inputs),('arithmetic','x87_extended_host.c',a.arithmetic_inputs),('integer','x87_integer_host.c',a.integer_inputs)]
    results={};frozen={str(p.resolve()):sha(p) for p in (Path(__file__),a.native)}
    for name,filename,inputs in cases:
        source=pc/'tests'/filename;exe=out/(name+'.exe');frozen[str(source)]=sha(source);frozen[str(inputs.resolve())]=sha(inputs)
        r=subprocess.run([*proof['compiler'],*proof['flags'],str(source),'@'+str(rsp),'-o',str(exe)],capture_output=True,text=True)
        (out/(name+'-compiler.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr)
        r=subprocess.run([str(exe)],input=inputs.read_bytes(),capture_output=True)
        (out/(name+'.bin')).write_bytes(r.stdout);(out/(name+'.log')).write_bytes(r.stderr)
        if r.returncode:raise RuntimeError(name+': '+r.stderr.decode(errors='replace'))
        results[name]=r.stderr.decode().strip()
    if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Oracle input changed')
    report=dict(status='PASS',real_x87_oracles=results,source_sha256=frozen,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('native','control-inputs','arithmetic-inputs','integer-inputs','output'):p.add_argument('--'+k,type=Path,required=True)
    run(p.parse_args())

"""Compile and admit explicit leaf inlining in retained private generated units.

Admission reconstructs the source transformation, binds the original CPU objects
and checks ARM64 relocation removal. No game bodies are embedded in this tool.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from inline_generated_leaves import transform

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def candidate(proof,gen,cpu):
    r=json.loads(Path(proof).read_text())
    if r.get('status')!='INLINE_CANDIDATE_GENERATED':raise ValueError('Invalid inline source proof')
    for name,digest in r['source_sha256'].items():
        if sha(name)!=digest:raise ValueError('Changed inline input')
    pc=Path(__file__).resolve().parents[1]
    tool=str((pc/'tools/inline_generated_leaves.py').resolve())
    if r['source_sha256'].get(tool)!=sha(tool):raise ValueError('Missing transform identity')
    # Every source, including those supplying leaf bodies, must belong to this
    # exact generated baseline and still be the object selected by the CPU link.
    for source in {r['unit'],*r['leaves'].values()}:
        rows=[v for v in gen['compiled'] if Path(v['source']).resolve()==Path(source).resolve()]
        if len(rows)!=1:raise ValueError('Inline source lacks generated provenance')
        row=rows[0]
        if gen['source_sha256'].get(str(Path(source).resolve()))!=sha(source):raise ValueError('Changed generated source')
        if cpu['source_sha256'].get(row['object'])!=row['sha256'] or sha(row['object'])!=row['sha256']:raise ValueError('CPU selected a different original object')
    text,info=transform(Path(r['unit']).read_text(),{n:Path(p).read_text() for n,p in r['leaves'].items()},set(r['callers']))
    if text!=Path(r['output']).read_text() or sha(r['output'])!=r['output_sha256'] or info['call_sites']!=r['call_sites'] or info['leaf_body_sha256']!=r['leaf_body_sha256']:raise ValueError('Inline transformation differs')
    return r,next(v for v in gen['compiled'] if Path(v['source']).resolve()==Path(r['unit']).resolve())

def verify(path,cpu_path):
    r=json.loads(Path(path).read_text());cpu=json.loads(Path(cpu_path).read_text());gen_path=Path(r['generated_validation']);gen=json.loads(gen_path.read_text())
    if r.get('status')!='INLINE_UNITS_COMPILED' or r['cpu_validation_sha256']!=sha(cpu_path) or r['generated_sha256']!=sha(gen_path):raise ValueError('Wrong inline baseline')
    if gen_path.resolve()!=Path(cpu['inputs']['generated_validation']).resolve():raise ValueError('Wrong generated provenance')
    if gen['status']!='COMPILED' or gen['compiler_flags']!=r['compiler_flags']:raise ValueError('Changed inline flags')
    oracle=json.loads(Path(r['oracle']).read_text())
    if oracle.get('status')!='PASS' or oracle.get('synthetic_cases',0)<12000 or oracle.get('compared_state_bytes')!=4132 or oracle.get('negative_cases',0)<6:raise ValueError('Unverified inline semantics')
    for name,digest in oracle['source_sha256'].items():
        if sha(name)!=digest:raise ValueError('Changed inline oracle')
    for name,digest in r['source_sha256'].items():
        if sha(name)!=digest:raise ValueError('Changed compiled inline input')
    if r['source_sha256'].get(str(Path(r['oracle']).resolve()))!=sha(r['oracle']):raise ValueError('Unbound inline oracle')
    result={}
    for row in r['compiled']:
        c,old=candidate(row['candidate'],gen,cpu)
        expected=list(old['command']);expected[expected.index('-c')+1]=c['output'];expected[expected.index('-o')+1]=row['object']
        if row['command']!=expected or sha(row['object'])!=row['sha256']:raise ValueError('Changed inline object/command')
        if row['original_object']!=old['object'] or row['original_sha256']!=old['sha256']:raise ValueError('Wrong inline object binding')
        if row['removed_calls']!=sum(c['call_sites'].values()) or row['remaining_leaf_calls']!=0:raise ValueError('Inline machine-code evidence differs')
        key=str(Path(old['object']).resolve())
        if key in result:raise ValueError('Duplicate inline replacement')
        result[key]=Path(row['object']).resolve()
    if not result:raise ValueError('Empty inline set')
    return r,result

def body(dump,name):
    at=dump.index('<'+name+'>:');end=dump.find('\nDisassembly of section',at)
    return dump[at:end if end>=0 else None]

def run(a):
    cpu=json.loads(a.cpu.read_text());gen_path=Path(cpu['inputs']['generated_validation']);gen=json.loads(gen_path.read_text())
    frozen={str(p.resolve()):sha(p) for p in (Path(__file__),a.cpu,gen_path,a.oracle,a.objdump,Path(__file__).with_name('inline_generated_leaves.py'))}
    a.output.mkdir(parents=True,exist_ok=False);compiled=[]
    for proof in a.candidate:
        frozen[str(proof.resolve())]=sha(proof);c,old=candidate(proof,gen,cpu)
        for n,d in c['source_sha256'].items():frozen[n]=d
        frozen[c['output']]=sha(c['output']);cmd=list(old['command']);obj=(a.output/Path(old['object']).name).resolve()
        cmd[cmd.index('-c')+1]=c['output'];cmd[cmd.index('-o')+1]=str(obj);frozen[str(Path(cmd[0]).resolve())]=sha(cmd[0])
        for item in cmd:
            if item.startswith('-I'):
                for p in Path(item[2:]).rglob('*.h'):frozen[str(p.resolve())]=sha(p)
        p=subprocess.run(cmd,capture_output=True,text=True);(a.output/(obj.stem+'.log')).write_text(p.stdout+p.stderr)
        if p.returncode:raise RuntimeError('Inline compile failed: '+p.stderr[-1500:])
        dumps=[]
        for label,path in [('before',old['object']),('after',obj)]:
            text=subprocess.run([str(a.objdump),'-dr',str(path)],capture_output=True,text=True,check=True).stdout
            (a.output/(obj.stem+'-'+label+'.asm')).write_text(text);dumps.append(text)
        counts=[]
        for dump in dumps:
            counts.append(sum(len(re.findall(r'R_AARCH64_CALL26\s+'+name+r'\b',body(dump,caller))) for caller in c['callers'] for name in c['leaves']))
        if counts!=[sum(c['call_sites'].values()),0]:raise ValueError('Calls not removed exactly')
        if obj.stat().st_size>Path(old['object']).stat().st_size*1.1:raise ValueError('Excess inline growth')
        compiled.append(dict(candidate=str(proof.resolve()),original_object=old['object'],original_sha256=old['sha256'],object=str(obj),sha256=sha(obj),command=cmd,removed_calls=counts[0],remaining_leaf_calls=counts[1]))
    if any(sha(n)!=d for n,d in frozen.items()):raise ValueError('Inline input changed during compile')
    report=dict(status='INLINE_UNITS_COMPILED',cpu_validation_sha256=sha(a.cpu),generated_validation=str(gen_path),generated_sha256=sha(gen_path),compiler_flags=gen['compiler_flags'],oracle=str(a.oracle.resolve()),source_sha256=frozen,compiled=compiled,hardware_tested=False)
    target=a.output/'validation.json';target.write_text(json.dumps(report,indent=2)+'\n');verify(target,a.cpu)
    print(json.dumps(dict(status=report['status'],units=len(compiled),removed_calls=sum(r['removed_calls'] for r in compiled))))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('cpu','oracle','objdump','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--candidate',type=Path,action='append',required=True);run(p.parse_args())

"""Inline explicitly selected small generated leaf bodies into hot callers.

No game source is embedded here. Inputs/outputs containing generated game code
remain private. This preserves each leaf's guest stack and TLS-register writes;
it is not an algorithm replacement or permission to remove volatile accesses.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

TOKEN=re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*',re.S)
NAME=re.compile(r'sub_[0-9A-F]{8}\Z')
ALLOWED={'PUSH32','POP32','MEM8','MEM16','MEM32','MEM64','SET_LO8','SET_LO16',
         'CMP_EQ','CMP_NE','CMP_LT','CMP_LE','CMP_GT','CMP_GE','if','sizeof',
         'uint32_t','int32_t','uint16_t','int16_t','uint8_t','int8_t','uint64_t','int64_t'}

def mask(text):
    return TOKEN.sub(lambda m:''.join('\n' if c=='\n' else ' ' for c in m[0]),text)

def function_span(text,name):
    if not NAME.fullmatch(name):raise ValueError('Invalid function name')
    code=mask(text);matches=list(re.finditer(r'\bvoid\s+'+name+r'\s*\(\s*void\s*\)\s*\{',code))
    if len(matches)!=1:raise ValueError('Require one function definition: '+name)
    start=matches[0].start();opening=matches[0].end()-1;depth=1
    for i in range(opening+1,len(code)):
        depth+=(code[i]=='{')-(code[i]=='}')
        if not depth:return start,i+1
    raise ValueError('Unterminated function: '+name)

def leaf(text,name):
    start,end=function_span(text,name);body=text[start:end];code=mask(body)
    inside=code[code.index('{')+1:-1]
    if len(body)>8192 or inside.count(';')>100:raise ValueError('Leaf too large')
    if '#' in inside or re.search(r'\b(static|extern|_Thread_local|_Atomic|asm|__asm__)\b',inside):raise ValueError('Unsupported leaf storage/directive')
    calls=set(re.findall(r'\b([A-Za-z_]\w*)\s*\(',inside))
    if calls-ALLOWED:raise ValueError('Non-leaf/unknown operation: '+','.join(sorted(calls-ALLOWED)))
    if re.search(r'\bsub_[0-9A-F]{8}\b',inside):raise ValueError('Function identity reference in leaf')
    if re.search(r'\b(__LINE__|__FILE__|__COUNTER__|__func__|__FUNCTION__)\b',inside):raise ValueError('Leaf source identity is observable')
    return body

def transform(unit,callees,callers):
    if not callees or not callers or set(callees)&set(callers):raise ValueError('Require distinct explicit caller/callee sets')
    if re.search(r'\b(__LINE__|__FILE__|__COUNTER__|__func__|__FUNCTION__)\b',mask(unit)):raise ValueError('Source identity is observable')
    if 'FNV_INLINE_CALL' in mask(unit) or 'fnv_inline_sub_' in mask(unit):raise ValueError('Inline namespace already present')
    bodies={n:leaf(source,n) for n,source in callees.items()}
    spans=[(*function_span(unit,n),n) for n in callers]
    edits=[];counts={n:0 for n in callees};caller_counts={}
    for start,end,caller in spans:
        original=unit[start:end];rewritten=original;hits=0
        for name in callees:
            pattern=re.compile(r'\bRECOMP_ABI_CALL\s*\(\s*(0x[0-9A-Fa-f]+[uU]?)\s*,\s*('+name+r')\s*\)')
            # Operate on masked code; comments and strings must stay untouched.
            matches=list(pattern.finditer(mask(rewritten)))
            for m in reversed(matches):
                if int(m[1].rstrip('uU'),16)!=int(name[4:],16):raise ValueError('Mismatched call address')
                left,right=m.span(2)
                replacement=rewritten[m.start():left]+'fnv_inline_'+name+rewritten[right:m.end()]
                replacement=replacement.replace('RECOMP_ABI_CALL','FNV_INLINE_CALL',1)
                rewritten=rewritten[:m.start()]+replacement+rewritten[m.end():]
            counts[name]+=len(matches);hits+=len(matches)
        caller_counts[caller]=hits;edits.append((start,end,rewritten))
    if not all(counts.values()):raise ValueError('Each selected leaf must have a direct call site')
    result=unit
    for start,end,body in sorted(edits,reverse=True):result=result[:start]+body+result[end:]
    first=re.search(r'\bvoid\s+sub_[0-9A-F]{8}\s*\(\s*void\s*\)\s*\{',mask(result))
    if not first:raise ValueError('No insertion boundary')
    definitions=[]
    for name in sorted(bodies):
        body=re.sub(r'\bvoid\s+'+name,'static inline __attribute__((always_inline)) void fnv_inline_'+name,bodies[name],count=1)
        definitions.append(body)
    wrapper='#define FNV_INLINE_CALL(va,fn) do { __asm__ volatile("" ::: "memory"); RECOMP_ABI_CALL(va,fn); __asm__ volatile("" ::: "memory"); } while (0)\n'
    result=result[:first.start()]+wrapper+'\n\n'.join(definitions)+'\n\n'+result[first.start():]
    # Every unselected function and every selected body except direct-call name
    # tokens is retained as text (CLI inputs normalize newlines). No globals or
    # register ABI are changed.
    return result,dict(call_sites=counts,caller_sites=caller_counts,
                       leaf_body_sha256={n:hashlib.sha256(b.encode()).hexdigest() for n,b in bodies.items()})

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--unit',type=Path,required=True);p.add_argument('--caller',action='append',required=True)
    p.add_argument('--leaf',action='append',required=True,help='sub_XXXXXXXX=source.c')
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    callees={};leaf_paths={};inputs={str(a.unit.resolve()):hashlib.sha256(a.unit.read_bytes()).hexdigest(),str(Path(__file__).resolve()):hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}
    for spec in a.leaf:
        name,path=spec.split('=',1)
        if name in callees:raise ValueError('Duplicate leaf')
        path=Path(path);callees[name]=path.read_text();leaf_paths[name]=str(path.resolve());inputs[str(path.resolve())]=hashlib.sha256(path.read_bytes()).hexdigest()
    if a.output.exists():raise ValueError('Fresh private output required')
    text,report=transform(a.unit.read_text(),callees,set(a.caller));a.output.mkdir(parents=True)
    if any(hashlib.sha256(Path(n).read_bytes()).hexdigest()!=d for n,d in inputs.items()):raise ValueError('Input changed while generating')
    target=a.output/a.unit.name;target.write_text(text,newline='\n')
    report.update(status='INLINE_CANDIDATE_GENERATED',source_sha256=inputs,
                  unit=str(a.unit.resolve()),callers=sorted(set(a.caller)),leaves=leaf_paths,
                  output=str(target.resolve()),output_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),hardware_verified=False)
    (a.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status=report['status'],call_sites=report['call_sites'])))

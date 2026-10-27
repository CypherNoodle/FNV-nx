"""Compare private generated C bodies; never execute or publish the input code.

This is normalized text equality, not a semantic proof or an execution oracle.
Additional candidate functions are reported, never implicitly registered.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

FUNCTION=re.compile(r'^void ([A-Za-z_]\w*)\(void\)\s*\{',re.M)
TOKENS=re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',re.S)
TRACE_DEFINE='#define FNV_STARTUP_ICALL(target, saved, site) do { fnv_probe_site=(site); RECOMP_ICALL_SAFE_AT(target,saved,site); } while(0)'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def bodies(text):
    end=0;blocked=[(m.start(),m.end()) for m in TOKENS.finditer(text) if m.group()[0] in '/"\''];cursor=0
    for match in FUNCTION.finditer(text):
        while cursor<len(blocked) and blocked[cursor][1]<=match.start():cursor+=1
        if cursor<len(blocked) and blocked[cursor][0]<=match.start()<blocked[cursor][1]:continue
        if match.start()<end:raise ValueError('Nested generated function definition')
        depth=0
        for token in TOKENS.finditer(text,match.end()-1):
            if token.group()=='{':depth+=1
            elif token.group()=='}':
                depth-=1
                if not depth:
                    end=token.end();break
        else:raise ValueError('Unterminated generated function')
        yield match.group(1),text[match.start():end]


def inventory(files,normalize,frozen):
    result={}
    for path in files:
        frozen[str(path)]=sha(path)
        for name,body in bodies(path.read_text()):
            if name in result:raise ValueError('Duplicate generated body: '+name)
            if normalize:body=re.sub(r'\bFNV_STARTUP_ICALL\(', 'RECOMP_ICALL_SAFE_AT(',body)
            body='\n'.join(line.rstrip() for line in body.splitlines()).strip()
            result[name]=dict(source=str(path),sha256=hashlib.sha256(body.encode()).hexdigest())
    if not result:raise ValueError('No generated functions')
    return result


def audit(base,candidate,supplements=(),trace_header=None):
    frozen={str(Path(__file__).resolve()):sha(Path(__file__).resolve())}
    if trace_header:
        source=trace_header.read_text()
        if source.count('#define FNV_STARTUP_ICALL')!=1 or TRACE_DEFINE not in source.splitlines():
            raise ValueError('Unverified call-trace wrapper')
        frozen[str(trace_header)]=sha(trace_header)
    def units(root):
        found=[p.resolve() for p in sorted(root.glob('*.c'))
               if re.fullmatch(r'(?:recomp|generated)_\d{4}\.c',p.name)]
        if not found:raise ValueError('No generated C units in '+str(root))
        return found
    left=inventory(units(base),False,frozen)
    right=inventory([*units(candidate),*[p.resolve() for p in supplements]],bool(trace_header),frozen)
    common=left.keys()&right.keys()
    different=[name for name in sorted(common) if left[name]['sha256']!=right[name]['sha256']]
    additional={name:right[name] for name in sorted(right.keys()-left.keys())}
    if any(sha(Path(p))!=digest for p,digest in frozen.items()):raise ValueError('Audit inputs changed')
    return dict(status='BODY_MATCH' if not different else 'BODY_MISMATCH',
                comparison_kind='normalized-function-text-v1',base_functions=len(left),
                candidate_functions=len(right),common_functions=len(common),identical_functions=len(common)-len(different),
                different_functions=different,candidate_only=additional,base_only=sorted(left.keys()-right.keys()),
                call_trace_wrapper_normalized=bool(trace_header),source_sha256=frozen,
                functions_registered=False,objects_compiled=False,game_started=False,switch_hardware_tested=False)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('base','candidate','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--candidate-source',type=Path,action='append',default=[])
    p.add_argument('--trace-header',type=Path)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Use a fresh audit output')
    result=audit(a.base.resolve(),a.candidate.resolve(),a.candidate_source,a.trace_header.resolve() if a.trace_header else None)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('status','base_functions','candidate_functions','common_functions','identical_functions','different_functions','candidate_only')}))
    if result['status']!='BODY_MATCH':raise SystemExit(1)

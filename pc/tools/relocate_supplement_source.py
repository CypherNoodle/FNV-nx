"""Recover a missing private supplementary source from an accepted link copy.

Requires identical normalized body, pinned compiled source/link artifact and a
fresh independently reproduced x86 boundary audit. Preserves original proofs;
outputs new private metadata. Never reconstructs from or publishes binaries.
"""
import argparse,hashlib,json,re,shutil
from pathlib import Path
from audit_generation_body_bridge import bodies
from audit_gap_function import audit
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(a):
    if a.output.exists():raise ValueError('Use a fresh private directory')
    if not re.fullmatch('sub_[0-9A-F]{8}',a.function):raise ValueError('Invalid function')
    proof=json.loads(a.accepted_link.read_text());comparison=json.loads(a.body_audit.read_text());entries=json.loads(a.entries.read_text());boundary=json.loads(a.boundary_audit.read_text())
    if proof.get('status')!='PARTIAL_LINKED' or comparison.get('status')!='BODY_MATCH':raise ValueError('Require accepted link and body comparison')
    linked=Path(proof['runtime_object'])
    if sha(linked)!=proof['runtime_sha256']:raise ValueError('Changed accepted link artifact')
    source=a.accepted_link.parent/(a.function+'.c');old=Path(comparison['candidate_only'][a.function]['source'])
    if old.exists() or sha(source)!=proof['source_sha256'].get(str(source.resolve())):raise ValueError('Source is not missing or accepted copy changed')
    found=dict(bodies(source.read_text()))
    if set(found)!={a.function}:raise ValueError('Ambiguous accepted source')
    digest=hashlib.sha256('\n'.join(line.rstrip() for line in found[a.function].splitlines()).strip().encode()).hexdigest()
    if digest!=comparison['candidate_only'][a.function]['sha256']:raise ValueError('Accepted body differs from baseline')
    start=int(a.function[4:],16)
    if boundary.get('status')!='AUDITED_GAP_ENTRY' or boundary['start']!=start or boundary['functions_sha256']!=sha(a.functions):raise ValueError('Wrong fresh boundary evidence')
    regenerated=audit(a.code.read_bytes(),a.base,start,boundary['end'],json.loads(a.functions.read_text()),a.reference.read_bytes())
    if regenerated!={k:v for k,v in boundary.items() if k!='functions_sha256'}:raise ValueError('Boundary audit not reproducible')
    selected=[e for e in entries['entries'] if e['function']==a.function]
    if len(selected)!=1 or Path(selected[0]['source']).resolve()!=old.resolve():raise ValueError('Manifest mismatch')
    frozen={str(p.resolve()):sha(p) for p in (a.accepted_link,a.body_audit,a.entries,a.boundary_audit,a.code,a.functions,a.reference,source,Path(__file__))}
    a.output.mkdir(parents=True);out=a.output.resolve();recovered=out/source.name;shutil.copyfile(source,recovered)
    comparison['candidate_only'][a.function]['source']=str(recovered)
    del comparison['source_sha256'][str(old)]
    comparison['source_sha256'].update(frozen);comparison['source_sha256'][str(recovered)]=sha(recovered)
    comparison['relocated_input']=dict(missing_source=str(old),recovered_source=str(recovered),normalized_body_sha256=digest,semantic_equivalence_proved=False)
    selected[0]['source']=str(recovered);selected[0]['boundary_evidence']=str(a.boundary_audit.resolve());selected[0]['boundary_evidence_sha256']=sha(a.boundary_audit)
    (out/'body-audit.json').write_text(json.dumps(comparison,indent=2)+'\n');(out/'entries.json').write_text(json.dumps(entries,indent=2)+'\n')
    if any(sha(Path(p))!=h for p,h in frozen.items()):raise ValueError('Recovery inputs changed')
    result=dict(status='IDENTICAL_BODY_RELOCATED',function=a.function,body_sha256=digest,source_sha256=frozen,private_output=True)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(result['status'],a.function)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('accepted-link','body-audit','entries','boundary-audit','code','functions','reference','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--base',type=lambda s:int(s,0),required=True);p.add_argument('--function',required=True);run(p.parse_args())

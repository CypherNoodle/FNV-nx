"""Inject invalid metadata into a private accepted CPU/runtime link contract."""
import argparse
import copy
import json
from pathlib import Path
import re
from types import SimpleNamespace
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from link_switch_engine_cpu import build
from run_aot_analysis import sha


def run(a):
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    proof=json.loads(a.accepted_build.read_text());tool=Path(__file__).resolve().parents[1]/'tools/link_switch_engine_cpu.py'
    if proof['status']!='PARTIAL_LINKED' or proof['source_sha256'].get(str(tool))!=sha(tool):
        raise ValueError('Require the current accepted linker proof')
    if sha(Path(proof['runtime_object']))!=proof['runtime_sha256']:raise ValueError('Accepted artifact changed')
    original=SimpleNamespace(**{name:Path(value) for name,value in proof['inputs'].items()})
    evidence=json.loads(original.entries.read_text());audit=json.loads(original.body_audit.read_text())
    dispatch=(original.generation/'recomp_dispatch.c').read_text()
    occupied=int(re.search(r'\{\s*0x([0-9A-Fa-f]{8})[uUlL]*\s*,',dispatch)[1],16)
    cases=[]
    for name in ('missing-entry','changed-body','failed-object','existing-address','required-group-entry'):
        args=copy.copy(original);args.output=out/name
        if name=='missing-entry':
            data=copy.deepcopy(evidence);data['entries']=data['entries'][:-1];key='entries';expected='cover exactly'
        elif name=='changed-body':
            data=copy.deepcopy(audit);first=next(iter(data['candidate_only']));data['candidate_only'][first]['sha256']='0'*64;key='body_audit';expected='Supplementary body changed'
        elif name=='failed-object':
            data=json.loads(original.generated_validation.read_text());data['compiled'][0]['exit_code']=1;key='generated_validation';expected='invalid generated object'
        elif name=='required-group-entry':
            if not evidence.get('required_entry_groups'):raise ValueError('Require a build with mandatory dispatch groups')
            required=json.loads(Path(evidence['required_entry_groups'][0]['evidence']).read_text())['recovered_cfg_entries'][0]['start']
            data=copy.deepcopy(evidence);removed=next(e for e in data['entries'] if e['address']==required)
            data['entries'].remove(removed);changed=copy.deepcopy(audit);del changed['candidate_only'][removed['function']]
            path=out/'required-group-audit.json';path.write_text(json.dumps(changed)+'\n');args.body_audit=path
            key='entries';expected='Missing required audited dispatch entries'
        else:
            data=copy.deepcopy(evidence);old=data['entries'][0]['function'];new=f'sub_{occupied:08X}'
            data['entries'][0].update(address=occupied,function=new)
            changed=copy.deepcopy(audit);changed['candidate_only'][new]=changed['candidate_only'].pop(old)
            path=out/'existing-address-audit.json';path.write_text(json.dumps(changed)+'\n');args.body_audit=path
            key='entries';expected='override the existing dispatcher'
        path=out/(name+'.json');path.write_text(json.dumps(data)+'\n');setattr(args,key,path)
        try:build(args)
        except ValueError as error:
            if expected not in str(error):raise AssertionError(str(error)) from error
        else:raise AssertionError(name+' unexpectedly linked')
        assert not args.output.exists();cases.append(dict(case=name,rejected_before_output=True))
    result=dict(status='PASS',cases=cases,accepted_link_sha256=sha(a.accepted_build),
                source_sha256={str(Path(__file__).resolve()):sha(Path(__file__).resolve()),str(tool):sha(tool)},
                game_started=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('accepted-build','output'):p.add_argument('--'+name,type=Path,required=True)
    run(p.parse_args())

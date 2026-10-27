"""Reject altered private CFG replacement metadata before creating link output."""
import argparse
import copy
import json
from pathlib import Path
import sys
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from link_switch_engine_cpu import build
from run_aot_analysis import sha

def run(a):
    proof=json.loads(a.accepted_build.read_text());out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    if proof.get('status')!='PARTIAL_LINKED' or proof.get('cfg_corrected_units')!=1:raise ValueError('Require accepted one-unit corrected CPU link')
    original=SimpleNamespace(**{k:Path(v) for k,v in proof['inputs'].items()})
    manifest=json.loads(original.corrections.read_text());record=json.loads(Path(manifest[0]['proof']).read_text())
    cases=[]
    for case in ('changed-source','changed-object','foreign-unit'):
        altered=copy.deepcopy(record)
        if case=='changed-source':altered['corrected_sha256']='0'*64;message='Changed corrected source/object'
        elif case=='changed-object':altered['object_sha256']='0'*64;message='Changed corrected source/object'
        else:altered['original_unit']=str(out/'not-in-generation.c');message='does not belong to generated inventory'
        evidence=out/(case+'.json');evidence.write_text(json.dumps(altered)+'\n')
        selected=out/(case+'-manifest.json');selected.write_text(json.dumps([dict(proof=str(evidence),sha256=sha(evidence))])+'\n')
        args=copy.copy(original);args.corrections=selected;args.output=out/case
        try:build(args)
        except ValueError as error:assert message in str(error),str(error)
        else:raise AssertionError('Invalid CFG correction accepted')
        assert not args.output.exists();cases.append(case)
    (out/'validation.json').write_text(json.dumps(dict(status='PASS',cases=cases,accepted_build_sha256=sha(a.accepted_build)),indent=2)+'\n')
    print('PASS',len(cases),'CFG correction admission guards')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--accepted-build',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    run(p.parse_args())

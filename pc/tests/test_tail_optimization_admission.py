"""Reject invalid tail-optimization certificates before creating link output."""
import argparse,copy,json,sys
from pathlib import Path
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from link_switch_engine_cpu import build
from run_aot_analysis import sha
def run(a):
    accepted=json.loads(a.accepted_build.read_text());args=SimpleNamespace(**{k:Path(v) for k,v in accepted['inputs'].items()})
    original=json.loads(a.tail_proof.read_text());manifest=json.loads(args.corrections.read_text())
    a.output.mkdir(parents=True,exist_ok=False);cases=[]
    for case in ('extra-flag','changed-source','missing-edges','wrong-machine-edge'):
        bad=copy.deepcopy(original)
        if case=='extra-flag':bad['optimizer_flags'].append('-ffast-math')
        elif case=='changed-source':bad['corrected_sha256']='0'*64
        elif case=='missing-edges':bad['verified_edges']=bad['verified_edges'][:1]
        else:bad['optimized_edges']='CALL26'
        proof=a.output/(case+'.json');proof.write_text(json.dumps(bad));records=copy.deepcopy(manifest)+[dict(proof=str(proof.resolve()),sha256=sha(proof))]
        m=a.output/(case+'-manifest.json');m.write_text(json.dumps(records));test=copy.copy(args);test.corrections=m;test.output=a.output/case
        try:build(test)
        except ValueError as error:assert 'Unverified tail optimization' in str(error),str(error)
        else:raise AssertionError('Invalid tail certificate accepted')
        assert not test.output.exists();cases.append(case)
    (a.output/'validation.json').write_text(json.dumps(dict(status='PASS',cases=cases),indent=2)+'\n');print('PASS',cases)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('accepted-build','tail-proof','output'):p.add_argument('--'+n,type=Path,required=True)
    run(p.parse_args())

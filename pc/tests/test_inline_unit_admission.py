"""Negative admission checks for compiled private inline candidates."""
import argparse
import copy
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build_inline_generated_units import verify

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('proof','cpu','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False);original=json.loads(a.proof.read_text());verify(a.proof,a.cpu)
    cases=[lambda r:r.update(cpu_validation_sha256='0'*64),
           lambda r:r.update(generated_sha256='0'*64),
           lambda r:r['compiler_flags'].append('-ffast-math'),
           lambda r:r['compiled'][0].update(original_sha256='0'*64),
           lambda r:r['compiled'][0].update(sha256='0'*64),
           lambda r:r['compiled'][0]['command'].append('-O3'),
           lambda r:r['compiled'][0].update(remaining_leaf_calls=1),
           lambda r:r['compiled'][0].update(removed_calls=0),
           lambda r:r['compiled'].append(copy.deepcopy(r['compiled'][0])),
           lambda r:r.update(compiled=[])]
    for i,change in enumerate(cases):
        report=copy.deepcopy(original);change(report);out=a.output/(str(i)+'.json');out.write_text(json.dumps(report))
        try:verify(out,a.cpu)
        except ValueError:pass
        else:raise AssertionError('Accepted invalid inline proof')
    (a.output/'validation.json').write_text(json.dumps(dict(status='PASS',negative_cases=len(cases)))+'\n')
    print('PASS: 10 invalid inline proofs rejected')

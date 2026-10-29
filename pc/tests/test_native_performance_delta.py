"""Reject altered native object provenance before packaging the Switch NRO."""
import argparse
import copy
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build_native_performance_delta import verify

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--proof',type=Path,required=True)
    p.add_argument('--baseline',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False);original=json.loads(a.proof.read_text())
    verify(a.proof,a.baseline)
    changes=[('baseline',lambda r:r.update(baseline_sha256='0'*64)),
             ('flags',lambda r:r['compiler_flags'].append('-ffast-math')),
             ('sdk',lambda r:r.update(sdk_sha256='0'*64)),
             ('unit',lambda r:r['compiled'][0].update(source='outside.c')),
             ('old-object',lambda r:r['compiled'][0].update(original_sha256='0'*64)),
             ('new-object',lambda r:r['compiled'][0].update(sha256='0'*64)),
             ('command',lambda r:r['compiled'][0]['command'].append('-ffast-math')),
             ('dependency',lambda r:r['source_sha256'].pop(next(n for n in r['source_sha256'] if n.endswith('resource_lookup.h')))),
             ('changed-input',lambda r:r['source_sha256'].update({next(n for n in r['source_sha256'] if n.endswith('audio_phase.h')):'0'*64}))]
    for name,change in changes:
        r=copy.deepcopy(original);change(r);out=a.output/(name+'.json');out.write_text(json.dumps(r))
        try:verify(out,a.baseline)
        except ValueError:pass
        else:raise AssertionError('Accepted invalid delta: '+name)
    (a.output/'validation.json').write_text(json.dumps(dict(status='PASS',negative_cases=len(changes),packaging_reached=False))+'\n')
    print('PASS: native flags, inputs, units, object hashes and baseline provenance; 9 negative cases')

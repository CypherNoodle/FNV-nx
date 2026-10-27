"""Reject missing, duplicate and inconsistent native units before ELF output."""
import argparse,copy,json,sys
from pathlib import Path
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from link_switch_engine_platform import build
def run(a):
    a.output.mkdir(parents=True,exist_ok=False);original=json.loads(a.runtime_validation.read_text());cases=[]
    for case in ('missing','duplicate','count'):
        bad=copy.deepcopy(original)
        if case=='missing':bad['compiled'].pop();bad['native_units']-=1
        elif case=='duplicate':bad['compiled'][-1]=bad['compiled'][0]
        else:bad['native_units']-=1
        p=a.output/(case+'.json');p.write_text(json.dumps(bad))
        args=SimpleNamespace(output=a.output/case,cpu_validation=a.cpu_validation,runtime_validation=p,mesa_validation=a.mesa_validation)
        try:build(args)
        except ValueError as error:assert 'native runtime archive inventory' in str(error),str(error)
        else:raise AssertionError('Invalid native inventory accepted')
        assert not args.output.exists();cases.append(case)
    (a.output/'validation.json').write_text(json.dumps(dict(status='PASS',cases=cases),indent=2)+'\n');print('PASS',cases)
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('cpu-validation','runtime-validation','mesa-validation','output'):p.add_argument('--'+n,type=Path,required=True)
    run(p.parse_args())

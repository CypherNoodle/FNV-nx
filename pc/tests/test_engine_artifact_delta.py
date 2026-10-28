"""Negative admission checks using private retained artifacts, never a game run."""
import argparse
import hashlib
import json
from pathlib import Path
from types import SimpleNamespace
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import build_switch_engine_artifact_delta as tool

def run(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True);recipe=json.loads(a.recipe.read_text())
    # No rejected recipe may reach compilation or packaging.
    def forbidden(*args,**kwargs):raise AssertionError('Rejected recipe reached packaging')
    tool.package=forbidden
    def reject(name,modified,expected,**extra):
        p=out/(name+'.json');p.write_text(json.dumps(modified))
        try:tool.run(SimpleNamespace(recipe=p,output=out/name,**extra))
        except ValueError as e:
            if expected not in str(e):raise
        else:raise AssertionError('Invalid recipe accepted: '+name)
    wrong=out/'wrong-baseline.nro';wrong.write_bytes(b'own invalid baseline')
    reject('wrong-baseline',{**recipe,'base_nro':str(wrong)},'Changed artifact')
    old=json.loads(Path(recipe['x87_core']).read_text())
    core=out/'unexpected-core.json'
    victim=next(r for r in old['objects'] if Path(r['object']).name=='x87_stack.o')
    own=out/'x87_stack.o';own.write_bytes(Path(victim['object']).read_bytes()+b'OWN_TEST')
    victim.update(object=str(own),sha256=hashlib.sha256(own.read_bytes()).hexdigest())
    core.write_text(json.dumps(old))
    reject('unexpected-object',{**recipe,'x87_core':str(core)},'Only own x87_extended.o may differ')
    old['objects'][0]['sha256']='0'*64;core2=out/'bad-core-proof.json';core2.write_text(json.dumps(old))
    reject('bad-object-digest',{**recipe,'x87_core':str(core2)},'Changed artifact')
    reject('unsupported-entry',recipe,'Unsupported entry source delta',entry_source_delta=['../engine_probe.c'])
    reject('duplicate-entry',recipe,'Duplicate entry source delta',entry_source_delta=['engine_probe.c','engine_probe.c'])
    sentinel=out/'unsafe-output/keep.txt';sentinel.parent.mkdir();sentinel.write_text('keep')
    reject('unsafe-output',recipe,'Fresh output required',replace_output=True)
    assert sentinel.read_text()=='keep'
    count=6
    if getattr(a,'private_core',None):
        private_recipe={**recipe,'x87_core':str(a.private_core.resolve())}
        reject('private-unverified',private_recipe,'Private state requires concurrency/TLS validation')
        bad=json.loads(a.private_proof.read_text());bad['horizon_state_symbols_tls']=0
        bad_path=out/'bad-private-proof.json';bad_path.write_text(json.dumps(bad))
        reject('private-bad-proof',private_recipe,'Unverified private-state core',private_state_proof=bad_path)
        count+=2
    (out/'validation.json').write_text(json.dumps(dict(status='PASS',cases=count,compilation_reached=False))+'\n')
    print('PASS: changed baseline, unexpected replacement, bad digest and unsupported/duplicate entry deltas rejected before packaging')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--recipe',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--private-core',type=Path);p.add_argument('--private-proof',type=Path)
    run(p.parse_args())

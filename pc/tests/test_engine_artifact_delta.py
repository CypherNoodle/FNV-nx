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
    def reject(name,modified,expected):
        p=out/(name+'.json');p.write_text(json.dumps(modified))
        try:tool.run(SimpleNamespace(recipe=p,output=out/name))
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
    (out/'validation.json').write_text(json.dumps(dict(status='PASS',cases=3,compilation_reached=False))+'\n')
    print('PASS: changed baseline, unexpected replacement and bad object digest rejected before packaging')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--recipe',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    run(p.parse_args())

"""Check target evaluator wire order, actual arithmetic and fail-closed proofs."""
import argparse
import json
from pathlib import Path
import shutil
import struct
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from x87_target_evaluator import Evaluator,sha


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);target=Evaluator(a.validation,a.python_deps);checks=0
    zero=bytes(10);one=struct.pack('<QH',1<<63,0x3fff)
    def raw(sig,exp):return struct.pack('<QH',sig,exp)
    def state(x,y=one,cw=0x37f,top=0,occupied=3):
        return x+y+one*6+struct.pack('<HHBB',cw,0,top,occupied)
    def run(kind,op,before):
        nonlocal checks
        result,after=target.evaluate(bytes((kind,op))+before);checks+=1
        assert after[80:82]==before[80:82]
        return result,after
    for rc in range(4):
        cw=0x37f|(rc<<10)
        for x,wanted in [(zero,zero),(raw(0,0x8000),raw(0,0x8000)),(raw(1<<63,0xbfff),raw(1<<63,0xbffe))]:
            result,after=run(0,0,state(x,cw=cw));assert result==0 and after[:10]==wanted and after[84:]==bytes((0,3))
        result,after=run(0,2,state(one,raw(0xc000000000000000,0x4000),cw));assert result==0 and after[10:20]==zero and after[84:]==bytes((1,2))
        result,after=run(0,1,state(one,zero,cw));assert result==0 and after[10:20]==zero and after[84:]==bytes((1,2))
        for op,wanted in ((0,zero),(1,one)):
            result,after=run(1,op,state(zero,cw=cw));assert result==0 and after[:10]==wanted and after[84:]==bytes((0,3))
    for kind,op,before,wanted in [(2,0,state(one),1),(0,3,state(one),1),
            (1,4,state(one),1),(0,0,state(one,occupied=0),2),
            (0,0,state(one,top=8),1),(0,0,state(one,cw=0x37e),4),
            (0,0,state(raw(0,0x4000)),4)]:
        result,after=run(kind,op,before);assert result==wanted and after==before
    def reject(call):
        nonlocal checks
        try:call()
        except ValueError:checks+=1
        else:raise AssertionError('Bad evaluator input accepted')
    reject(lambda:target.evaluate(b''));reject(lambda:target.evaluate(bytes(89)))
    proof=json.loads(a.validation.read_text());bad=a.output/'bad';bad.mkdir()
    for name in proof['generated_sha256']:shutil.copyfile(a.validation.parent/name,bad/name)
    changed=dict(proof);changed['input_bytes']=87;(bad/'validation.json').write_text(json.dumps(changed))
    reject(lambda:Evaluator(bad/'validation.json',a.python_deps))
    (bad/'validation.json').write_text(json.dumps(proof));p=bad/'probe.bin';b=p.read_bytes();p.write_bytes(bytes((b[0]^1,))+b[1:])
    reject(lambda:Evaluator(bad/'validation.json',a.python_deps))
    out=dict(status='PASS',checks=checks,evaluator_validation_sha256=sha(a.validation),
             source_sha256={str(Path(__file__).resolve()):sha(Path(__file__))},
             model='exact-compiled-arm64',game_inputs=False,hardware_tested=False)
    (a.output/'validation.json').write_text(json.dumps(out,indent=2)+'\n');print('PASS',checks,'ARM64 evaluator checks')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('validation','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    test(p.parse_args())

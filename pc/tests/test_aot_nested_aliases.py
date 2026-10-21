"""Nested aliases must be reconsidered after the SDK emits each alias."""
import argparse
import json
from pathlib import Path
import sys
from types import SimpleNamespace

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from aot_index import close_nested_tail_aliases
from run_aot_analysis import sha


class FakeDetector:
    def __init__(self):
        self._candidates={0x100:(1.0,'synthetic')}
        self._alias_entries={0x180:0x200}
        self.functions={a:SimpleNamespace(start=a,end=0x200) for a in (0x100,0x180)}
        self.engine=SimpleNamespace(instructions={},instruction_covering=lambda address:None)
        self.jumps={0x190:0x120,0x130:0x110}
        self.image=type('Image',(),{'get_code_sections':lambda _self:[]})()
        self.calls=0
    def first_pass(self,sections):
        assert sections is None
    def _pass_tail_jump_targets(self,sections):
        assert sections==[]
        self.calls+=1
        changed=False
        for source,target in self.jumps.items():
            source_owner=min((f for f,v in self.functions.items() if v.start<=source<v.end),
                             key=lambda f:self.functions[f].end-self.functions[f].start,default=None)
            target_owner=min((f for f,v in self.functions.items() if v.start<target<v.end),
                             key=lambda f:self.functions[f].end-self.functions[f].start,default=None)
            if source_owner is None or target_owner is None:continue
            lo,hi=self.functions[source_owner].start,self.functions[source_owner].end
            if not lo<=target<hi and target not in self._alias_entries:
                self._alias_entries[target]=self.functions[target_owner].end
                changed=True
        return changed
    def _build_functions(self,sections):
        self.functions={0x100:SimpleNamespace(start=0x100,end=0x200)}
    def _build_alias_entries(self):
        self.functions.update({a:SimpleNamespace(start=a,end=end) for a,end in self._alias_entries.items()})
    def _build_call_graph(self):
        pass


def test(output):
    if output.exists():raise ValueError('Fresh private output required')
    output.mkdir(parents=True)
    detector=FakeDetector()
    count=close_nested_tail_aliases(detector,None,lambda d,s:d.first_pass(s))
    assert count==4 and detector._alias_entries=={0x180:0x200,0x120:0x200,0x110:0x200}, (count,detector._alias_entries,detector.calls)
    assert (detector.functions[0x100].start,detector.functions[0x100].end)==(0x100,0x200) and detector.calls==3
    limited=FakeDetector()
    try:close_nested_tail_aliases(limited,None,lambda d,s:d.first_pass(s),max_rounds=1)
    except RuntimeError:pass
    else:raise AssertionError('Non-convergent alias closure accepted')
    sources=[Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'tools/aot_index.py']
    proof=dict(status='PASS',nested_aliases_recovered=2,closure_passes=3,
               owner_body_preserved=True,nonconvergent_scan_rejected=True,
               sdk_modified=False,source_sha256={str(p):sha(p) for p in sources})
    (output/'validation.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps(proof,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
    test(p.parse_args().output)

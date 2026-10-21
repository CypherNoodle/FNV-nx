"""Compare incremental address lookup to a fresh full-sort oracle."""
import random
import sys
from pathlib import Path
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from aot_index import IndexedInstructions, add_cross_owner_branch_aliases

def test():
    rng=random.Random(2718); data=IndexedInstructions()
    for address in range(0,20000,4):
        data[address]=SimpleNamespace(address=address,end_address=address+3)
    index=data.lookup_index()
    cases=0
    for step in range(1000):
        address=rng.randrange(20000)
        if step%3==0: data.pop(address,None)
        else: data[address]=SimpleNamespace(address=address,end_address=address+rng.randrange(1,16))
        lo=rng.randrange(20000); hi=lo+rng.randrange(250)
        expected=[data[a] for a in sorted(data) if lo<=a<hi]
        assert index.range(lo,hi)==expected
        expected=next((data[a] for a in reversed(sorted(data)) if lo-15<=a<lo<data[a].end_address),None)
        assert index.covering(lo)==expected
        cases+=2
    # Deleted/re-added base and incremental addresses never appear twice.
    for address in (8,9):
        data[address]=SimpleNamespace(address=address,end_address=address+1)
        data.pop(address); data[address]=SimpleNamespace(address=address,end_address=address+2)
    assert [i.address for i in index.range(8,10)]==[8,9]
    assert index.range(10,10)==[] and index.covering(0) is None
    print('PASS:',cases,'range/cover comparisons, insert/update/delete/re-add; instruction semantics unchanged')

def test_cross_owner_branch_aliases():
    class Engine:
        def __init__(self,instructions,covered=(),probing=True):
            self.instructions={i.address:i for i in instructions}
            self.covered=set(covered)
            self.probing=probing
        def instruction_covering(self,address):
            return SimpleNamespace(address=address-1) if address in self.covered else None
        def probes_as_function_body(self,address,max_insns=512):
            assert max_insns==512
            return self.probing

    class Detector:
        def __init__(self,functions,instructions,covered=(),probing=True):
            self.functions={f.start:f for f in functions}
            self._alias_entries={}
            self.engine=Engine(instructions,covered,probing)

    def branch(address,target):
        return SimpleNamespace(address=address,is_jump=True,is_cond_jump=False,jump_target=target)

    def entry(address):
        return SimpleNamespace(address=address,is_jump=False,is_cond_jump=False,jump_target=None)

    def owner(start,end,method='prologue'):
        return SimpleNamespace(start=start,end=end,detection_method=method)

    # Direct jumps from distinct callers cross into an instruction-aligned
    # block inside another body. The alias shares the receiver's end.
    target=0x1010
    detector=Detector([
        owner(0x1000,0x1080,'tail_jump_alias'),
        owner(0x2000,0x2010,'prologue'),
        owner(0x3000,0x3010,'tail_jump_alias'),
    ],[entry(target),branch(0x2004,target),branch(0x3004,target)])
    assert add_cross_owner_branch_aliases(detector)==1
    assert detector._alias_entries=={target:0x1080}

    # Reject weak single-site edges, instruction interiors, targets without
    # owners, failed bounded body probes, and branches that stay in their body.
    weak=Detector([owner(0x1000,0x1080,'tail_jump_alias'),
                   owner(0x2000,0x2010,'tail_jump_alias')],
                  [entry(target),branch(0x2004,target)])
    assert add_cross_owner_branch_aliases(weak)==0
    # Overlapping aliases of the same weak source do not turn one branch
    # instruction into multiple independent sites.
    overlapping=Detector([owner(0x1000,0x1080,'tail_jump_alias'),
                          owner(0x2000,0x2010,'tail_jump_alias'),
                          owner(0x2002,0x2010,'tail_jump_alias')],
                         [entry(target),branch(0x2004,target)])
    assert add_cross_owner_branch_aliases(overlapping)==0
    repeated=Detector([owner(0x1000,0x1080,'tail_jump_alias'),
                       owner(0x2000,0x2010,'tail_jump_alias')],
                      [entry(target),branch(0x2004,target),branch(0x2008,target)])
    assert add_cross_owner_branch_aliases(repeated)==1
    mid=Detector([owner(0x1000,0x1080),owner(0x2000,0x2010)],
                 [entry(target),branch(0x2004,target)],covered={target})
    assert add_cross_owner_branch_aliases(mid)==0
    no_owner=Detector([owner(0x2000,0x2010)],
                      [entry(target),branch(0x2004,target),branch(0x2008,target)])
    assert add_cross_owner_branch_aliases(no_owner)==0
    no_body=Detector([owner(0x1000,0x1080),owner(0x2000,0x2010)],
                     [entry(target),branch(0x2004,target),branch(0x2008,target)],probing=False)
    assert add_cross_owner_branch_aliases(no_body)==0
    internal=Detector([owner(0x1000,0x1080)],
                      [entry(target),branch(0x1020,target),branch(0x1030,target)])
    assert add_cross_owner_branch_aliases(internal)==0
    print('PASS: shared in-body aliases require cross-owner jumps, decoded boundaries, bounded body evidence and corroboration')

def test_overlapping_interval_index():
    class Interval:
        def __init__(self,start,end): self.start=start;self.end=end

    rng=random.Random(9051)
    intervals=[]
    for _ in range(1200):
        start=rng.randrange(10000)
        intervals.append(Interval(start,start+rng.randrange(1,5000)))
    intervals.sort(key=lambda item:(item.start,item.end))
    starts=[item.start for item in intervals]
    size=1
    while size<len(intervals): size*=2
    tree=[0]*(size*2)
    for index,item in enumerate(intervals): tree[size+index]=item.end
    for index in range(size-1,0,-1): tree[index]=max(tree[index*2],tree[index*2+1])
    def query(address):
        limit=__import__('bisect').bisect_right(starts,address);found=[]
        def visit(node,left,right):
            if left>=limit or tree[node]<=address:return
            if right-left==1:
                if left<len(intervals):found.append(intervals[left])
                return
            middle=(left+right)//2;visit(node*2,left,middle);visit(node*2+1,middle,right)
        visit(1,0,size);return found
    for address in range(0,10000,17):
        expected=[item for item in intervals if item.start<=address<item.end]
        actual=query(address)
        assert {(item.start,item.end) for item in actual}=={(item.start,item.end) for item in expected}
    print('PASS: augmented interval tree matches the linear oracle across overlapping ranges')

if __name__=='__main__':
    test()
    test_cross_owner_branch_aliases()
    test_overlapping_interval_index()

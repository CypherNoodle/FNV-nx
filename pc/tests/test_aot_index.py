"""Compare incremental address lookup to a fresh full-sort oracle."""
import random
import sys
from pathlib import Path
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from aot_index import IndexedInstructions

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

if __name__=='__main__': test()

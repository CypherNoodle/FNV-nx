"""Incremental instruction index and nested tail-alias closure for x86 scans.

The SDK's first tail-jump passes discover aliases from ordinary detected
functions. A newly emitted alias can itself jump into another existing body;
run the same conservative SDK pass to a fixed point so those shared tails get
their own entry without truncating the containing function. SDK sources remain
unchanged; additional alias rules require decoded boundaries and direct-jump
evidence, with bounded SDK body probes for shared in-body entries.
"""
import argparse
import bisect
import runpy
import sys
from pathlib import Path

class InstructionIndex:
    def __init__(self, instructions):
        self.instructions=instructions
        self.base=sorted(instructions)
        self.added=[]
    def added_address(self, address):
        i=bisect.bisect_left(self.base,address)
        if i<len(self.base) and self.base[i]==address: return
        j=bisect.bisect_left(self.added,address)
        if j==len(self.added) or self.added[j]!=address:
            self.added.insert(j,address)
    def range(self, start, end):
        addresses=[]
        for index in (self.base,self.added):
            lo,hi=bisect.bisect_left(index,start),bisect.bisect_left(index,end)
            addresses.extend(a for a in index[lo:hi] if a in self.instructions)
        return [self.instructions[a] for a in sorted(addresses)]
    def covering(self, address):
        for insn in reversed(self.range(address-15,address)):
            if insn.address<address<insn.end_address: return insn
        return None

class IndexedInstructions(dict):
    def __init__(self):
        super().__init__()
        self.index=None
    def __setitem__(self,key,value):
        fresh=key not in self
        super().__setitem__(key,value)
        if fresh and self.index is not None: self.index.added_address(key)
    def lookup_index(self):
        if self.index is None: self.index=InstructionIndex(self)
        return self.index

class BoundedDisassembler:
    def __init__(self, backend, limit):
        self.backend=backend
        self.limit=max(1,limit)
    def disasm(self, code, address, count=0):
        # Capstone decodes its whole input before yielding the first item.
        # decode_at will consume at most max_insns new instructions plus a
        # possible existing first instruction. x86 instructions are <=15 bytes.
        count=min(count or self.limit,self.limit)
        return self.backend.disasm(code[:count*15],address,count=count)
    def __getattr__(self,name): return getattr(self.backend,name)

def close_nested_tail_aliases(detector,sections,detect_all,max_rounds=16):
    """Re-run the SDK's verified tail pass after aliases become functions."""
    detect_all(detector,sections)
    scan_sections=sections if sections is not None else detector.image.get_code_sections()
    for _ in range(max_rounds):
        before_candidates=set(detector._candidates)
        before_aliases=dict(detector._alias_entries)
        detector._pass_tail_jump_targets(scan_sections)
        overlap_aliases=add_overlapping_tail_aliases(detector)
        branch_aliases=add_cross_owner_branch_aliases(detector)
        changed=(before_candidates!=set(detector._candidates) or
                 before_aliases!=detector._alias_entries)
        if not changed:
            break
        if overlap_aliases:
            print(f"  cross-owner terminal jump aliases: +{overlap_aliases}")
        if branch_aliases:
            print(f"  cross-owner in-body branch aliases: +{branch_aliases}")
        detector.functions.clear()
        detector._build_functions(scan_sections)
        detector._build_alias_entries()
    else:
        raise RuntimeError('Nested tail aliases did not reach a fixed point')
    detector._build_call_graph()
    return len(detector.functions)

def add_overlapping_tail_aliases(detector):
    """Recover terminal jumps hidden by a different overlapping owner.

    The SDK's ordinary pass picks the nearest function start for a jump's
    source. In overlapping/aliased ranges, that owner can contain the target
    even though another complete function ends at the jump and tail-enters a
    different indexed body. Preserve the destination body and add a callable
    alias only when the source function ends exactly at a decoded unconditional
    jump and the target is an existing instruction boundary in another body.
    """
    functions=sorted(detector.functions.values(),key=lambda f:(f.start,f.end))
    starts=[f.start for f in functions]
    added=0
    for source in functions:
        if source.end<=source.start:
            continue
        insn=detector.engine.instruction_covering(source.end-1)
        if (insn is None or insn.end_address!=source.end or not insn.is_jump or
                insn.is_cond_jump or insn.jump_target is None):
            continue
        target=insn.jump_target
        if (target in detector.functions or target in detector._alias_entries or
                target not in detector.engine.instructions or
                detector.engine.instruction_covering(target) is not None):
            continue
        if source.start<=target<source.end:
            continue
        index=bisect.bisect_right(starts,target)-1
        if index<0:
            continue
        destination=functions[index]
        if not destination.start<target<destination.end:
            continue
        detector._alias_entries[target]=destination.end
        added+=1
    return added

def add_cross_owner_branch_aliases(detector):
    """Register shared in-body destinations reached across measured owners.

    Some functions contain a direct jump to a decoded block inside a different
    overlapping function body. The SDK keeps the original body but misses an
    entry for that block, so generated callers hit an unresolved stub. Add an
    alias only when the target is a decoded instruction boundary inside an
    existing owner, a direct unconditional jump crosses an owner boundary, and
    the bounded SDK body probe reaches a terminator. The alias shares the
    nearest containing owner's end; it never clips or replaces that body.
    """
    functions=sorted(detector.functions.values(),key=lambda f:(f.start,f.end))
    starts=[f.start for f in functions]
    tree_size=1
    while tree_size<len(functions): tree_size*=2
    max_end=[0]*(tree_size*2)
    for index,function in enumerate(functions):
        max_end[tree_size+index]=function.end
    for index in range(tree_size-1,0,-1):
        max_end[index]=max(max_end[index*2],max_end[index*2+1])

    def containing(address):
        limit=bisect.bisect_right(starts,address)
        found=[]
        def visit(node,left,right):
            if left>=limit or max_end[node]<=address:
                return
            if right-left==1:
                if left<len(functions): found.append(functions[left])
                return
            middle=(left+right)//2
            visit(node*2,left,middle)
            visit(node*2+1,middle,right)
        if functions:
            visit(1,0,tree_size)
        return found

    cross_sources={}
    destination_cache={}
    source_cache={}
    instruction_count=len(detector.engine.instructions)
    for number,insn in enumerate(detector.engine.instructions.values(),1):
        if number%1_000_000==0:
            print(f"  cross-owner branch scan: {number}/{instruction_count} decoded instructions")
        if (not insn.is_jump or insn.is_cond_jump or insn.jump_target is None):
            continue
        target=insn.jump_target
        if (target in detector.functions or target in detector._alias_entries or
                target not in detector.engine.instructions):
            continue
        if target not in destination_cache:
            destination_cache[target]=containing(target)
        destinations=destination_cache[target]
        if not destinations or any(owner.start==target for owner in destinations):
            continue
        if insn.address not in source_cache:
            source_cache[insn.address]=containing(insn.address)
        sources=source_cache[insn.address]
        outside=[source for source in sources
                 if not source.start<target<source.end]
        if outside:
            evidence=cross_sources.setdefault(target,{})
            for source in outside:
                evidence[(source.start,insn.address)]=source

    added=0
    for target,sources in sorted(cross_sources.items()):
        if detector.engine.instruction_covering(target) is not None:
            continue
        # One strongly indexed source is enough; weak aliases need multiple
        # independent branch sites before they establish a shared entry.
        strong=any(source.detection_method in (
            'entry_point','known_address','prologue','call_target')
            for source in sources.values())
        if not strong and len({site for _,site in sources})<2:
            continue
        destinations=containing(target)
        destination=max(destinations,key=lambda function:function.start)
        if (destination.end<=target or
                not detector.engine.probes_as_function_body(target,max_insns=512)):
            continue
        detector._alias_entries[target]=destination.end
        added+=1
    return added

def install():
    from tools.disasm.engine import DisasmEngine
    from tools.disasm.functions import FunctionDetector
    original_init=DisasmEngine.__init__
    original_sweep=DisasmEngine.linear_sweep
    original_decode=DisasmEngine.decode_at
    def initialize(self,*args,**kwargs):
        original_init(self,*args,**kwargs)
        if self.instructions: raise ValueError('Unexpected nonempty initial SDK instruction map')
        self.instructions=IndexedInstructions()
    def sweep(self,*args,**kwargs):
        # Rebuild once after a full sweep, rather than inserting millions of
        # addresses into the incremental list during initial decoding.
        self.instructions.index=None
        return original_sweep(self,*args,**kwargs)
    DisasmEngine.__init__=initialize
    DisasmEngine.linear_sweep=sweep
    def decode(self,address,max_insns=4096):
        backend=self._cs
        self._cs=BoundedDisassembler(backend,max_insns+1)
        try: return original_decode(self,address,max_insns)
        finally: self._cs=backend
    DisasmEngine.decode_at=decode
    DisasmEngine.get_instructions_in_range=lambda self,start,end:self.instructions.lookup_index().range(start,end)
    DisasmEngine.instruction_covering=lambda self,address:self.instructions.lookup_index().covering(address)

    original_detect_all=FunctionDetector.detect_all
    def detect_with_nested_alias_closure(self,sections=None):
        return close_nested_tail_aliases(self,sections,original_detect_all)
    FunctionDetector.detect_all=detect_with_nested_alias_closure

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('arguments',nargs=argparse.REMAINDER)
    args=parser.parse_args()
    sys.path.insert(0,str(args.sdk.resolve()))
    install()
    sys.argv=['tools.disasm',*args.arguments]
    runpy.run_module('tools.disasm',run_name='__main__')

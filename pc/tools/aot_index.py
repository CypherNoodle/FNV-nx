"""Incremental instruction index for large x86 analysis inputs.
Only changes lookup cost, not instruction decoding or function detection.
SDK source files are never modified. Run as an explicit disasm-only wrapper.
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

def install():
    from tools.disasm.engine import DisasmEngine
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

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('arguments',nargs=argparse.REMAINDER)
    args=parser.parse_args()
    sys.path.insert(0,str(args.sdk.resolve()))
    install()
    sys.argv=['tools.disasm',*args.arguments]
    runpy.run_module('tools.disasm',run_name='__main__')

"""Stock/patched SDK analysis comparison on synthetic x86, never executed."""
import argparse
from pathlib import Path
from types import SimpleNamespace
import sys

def test(sdk,deps=None):
    sys.path.insert(0,str(sdk.resolve()))
    if deps: sys.path.insert(0,str(deps.resolve()))
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
    from tools.disasm.engine import DisasmEngine
    from aot_index import install
    class Image:
        def __init__(self,code):
            self.code=code
            self.base_address=0x1000
            self.image_size=len(code)
            self.section=SimpleNamespace(executable=True,virtual_addr=0x1000,virtual_size=len(code),name='.text')
        def get_section_at_va(self,address):
            return self.section if 0x1000<=address<0x1000+len(self.code) else None
        def get_section_data(self,section): return self.code
    def execute():
        results=[]
        for code in (b'\x90'*4096+b'\xc3',b'\xb8\x01\x00\x00\x00'+b'\x90'*4096+b'\xc3',b'\x90'*32+b'\xeb\x04'+b'\x90'*64):
            for maximum in (1,2,15,64):
                for initial in (None,0x1000,0x1002):
                    engine=DisasmEngine(Image(code))
                    if initial is not None:
                        instruction=next(engine._cs.disasm(code[initial-0x1000:],initial,count=1))
                        engine.instructions[initial]=engine._classify_instruction(instruction)
                    added=engine.decode_at(0x1000,maximum)
                    instructions=[(k,v.mnemonic,v.op_str,v.size) for k,v in sorted(engine.instructions.items())]
                    probes=[[(v.address,v.size) for v in engine.get_instructions_in_range(a,b)] for a,b in ((0x1000,0x1004),(0x1001,0x1020))]
                    covers=[(i.address,i.size) if (i:=engine.instruction_covering(a)) else None for a in range(0x1000,0x1010)]
                    results.append((added,instructions,probes,covers))
        return results
    stock=execute(); install(); patched=execute()
    assert stock==patched
    print('PASS:',len(stock),'stock/patched SDK decode + range/cover cases; bounds, existing starts and overlapping streams')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk',type=Path,required=True)
    parser.add_argument('--python-deps',type=Path)
    args=parser.parse_args()
    test(args.sdk,args.python_deps)

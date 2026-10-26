"""Differential exact ARM64 VM reuse, including poisoned state and concurrency."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import struct
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from x87_target_evaluator import Evaluator,sha,unchanged
from x87_reuse_evaluator import ReusingEvaluator,writable_ranges


def requests():
    values=[(0,0),(1<<63,0x3ffc),(3<<62,0x3ffe),(1<<63,0xbffe),
            (1<<63,0x3fff),(1<<63,0x4000),(1<<63,0x403f),
            (0xc000000000000001,0x7fff),(1,0)]
    out=[]
    for family,operations in ((0,3),(1,4)):
        for op in range(operations):
            for i,(significand,exponent) in enumerate(values):
                for precision in (0,2,3):
                    for rounding in range(4):
                        top=(i+precision+rounding)%8;registers=[struct.pack('<QH',1<<63,0x3fff)]*8
                        registers[top]=struct.pack('<QH',significand,exponent)
                        state=b''.join(registers)+struct.pack('<HHBB',0x7f|(precision<<8)|(rounding<<10),0,top,(1<<top)|(1<<((top+1)&7)))
                        out.append(bytes((family,op))+state)
    return out


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('evaluator','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    fresh=Evaluator(a.evaluator,a.python_deps);reuse=ReusingEvaluator(a.evaluator,a.python_deps)
    frozen={**reuse.report['source_sha256'],str(Path(__file__).resolve()):sha(Path(__file__))}
    inputs=requests();start=time.perf_counter();expected=[fresh.evaluate(q) for q in inputs];fresh_time=time.perf_counter()-start
    start=time.perf_counter()
    for i,(q,wanted) in enumerate(zip(inputs,expected)):
        if i%7==0:
            for at,data in reuse.initial:reuse.arm.mem_write(at,b'\x5a'*len(data))
            reuse.arm.mem_write(0x800000,b'\xcc'*0x10000)
            reuse.arm.reg_write(reuse.ar.UC_ARM64_REG_X19,0xdeadbeef)
            reuse.arm.reg_write(reuse.ar.UC_ARM64_REG_FPCR,0)
        assert reuse.evaluate(q)==wanted
    reused_time=time.perf_counter()-start
    with ThreadPoolExecutor(max_workers=4) as pool:
        assert list(pool.map(reuse.evaluate,inputs))==expected
    for q in (b'',bytes(87),bytes(89)):
        try:reuse.evaluate(q)
        except ValueError:pass
        else:raise AssertionError('Malformed request accepted')
    assert reuse.evaluate(inputs[0])==expected[0]
    # Independent structural negative controls for the mutable-section parser.
    elf=bytearray((a.evaluator.parent/'probe.elf').read_bytes())
    for bad in (b'',bytes(64),bytes(elf[:63])):
        try:writable_ranges(bad)
        except ValueError:pass
        else:raise AssertionError('Invalid ELF accepted')
    table=struct.unpack_from('<Q',elf,40)[0];count=struct.unpack_from('<H',elf,60)[0]
    at=next(table+i*64 for i in range(count) if struct.unpack_from('<Q',elf,table+i*64+8)[0]&3==3)
    for address in (0x80000,0x400000):
        bad=bytearray(elf);struct.pack_into('<Q',bad,at+16,address)
        try:writable_ranges(bad)
        except ValueError:pass
        else:raise AssertionError('Out-of-arena writable section accepted')
    unchanged(frozen)
    report=dict(status='PASS',differential_requests=len(inputs),concurrent_requests=len(inputs),
        poisoned_state_reset=True,malformed_controls=8,writable_bytes=sum(n for _,n in reuse.ranges),
        fresh_seconds=fresh_time,reused_seconds=reused_time,source_sha256=frozen,
        engine_started=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'}))


if __name__=='__main__':main()

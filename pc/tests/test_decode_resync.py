"""Synthetic table-tail misalignment reproducer; no game bytes or assets."""
import sys
from pathlib import Path
import argparse


def test(sdk, deps):
    sys.path[:0] = [str(sdk.resolve()), str(deps.resolve()), str(Path(__file__).resolve().parents[1]/'tools')]
    from tools.recomp.disasm import Disassembler
    # Linear stream: mov imm32, INC ESP, MOV segment, RET. True table case
    # starts inside the immediate and covers both stale instruction starts.
    raw = bytes.fromhex('b80000008b448ee4c3')
    old = Disassembler().disassemble_function(raw, 0x1000, 0x1009, [0x1004, 0x1008])
    assert any(a.end_address > b.address for a,b in zip(old,old[1:]))
    from aot_decode_resync import install
    install(); installed = Disassembler.disassemble_function; install()
    assert Disassembler.disassemble_function is installed
    decoder = Disassembler()
    got = decoder.disassemble_function(raw,0x1000,0x1009,[0x1004,0x1008])
    assert [(i.address,i.size) for i in got] == [(0x1004,4),(0x1008,1)]
    assert not any(i.mnemonic=='inc' for i in got)
    assert [(i.address,i.size) for i in decoder.disassemble_function(raw,0x1000,0x1009,[0x1008,0x1004,0x1004])] == [(0x1004,4),(0x1008,1)]
    simple=bytes.fromhex('5589e55dc3')
    assert [(i.address,i.size) for i in decoder.disassemble_function(simple,0x2000,0x2005)] == [(0x2000,1),(0x2001,2),(0x2003,1),(0x2004,1)]
    assert decoder.disassemble_function(raw,0x1000,0x1000,[0x1000]) == []
    try:
        decoder.disassemble_function(raw,0x1000,0x1009,[0x1004,0x1005])
    except ValueError:
        pass
    else:
        raise AssertionError('Conflicting true entry points accepted')
    print('PASS stale-overlap reproducer, corrected boundaries, baseline decode, conflict rejection and idempotence')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,required=True);p.add_argument('--python-deps',type=Path,required=True)
    a=p.parse_args();test(a.sdk,a.python_deps)

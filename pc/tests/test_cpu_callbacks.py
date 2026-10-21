"""Verify nested translated stdcall callbacks through native import bridges."""
import argparse
from pathlib import Path
import struct
import json
import sys
import test_windows_heap as shared

def programs(raw):
    iat=struct.unpack_from('<I',raw,0x310)[0]+0x400000
    # add two arguments, mutate volatile XMM/x87, and return stdcall.
    add=bytes.fromhex('8b44240403442408660fefc0d9e8c20800')
    # Calls the same native bridge, which invokes the inner guest callback.
    nested=b'\x68'+struct.pack('<I',0x401700)+b'\xff\x15'+struct.pack('<I',iat)+bytes.fromhex('83c003c20800')
    return {0x401700:add,0x401780:nested,0x401800:bytes.fromhex('31c0c3'),
            0x401880:bytes.fromhex('bb0100000031c0c20800'),
            0x401900:bytes.fromhex('f4c20800')}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    p.add_argument('--x87-build',type=Path)
    a=p.parse_args();setup=None;extra=[Path(__file__).resolve()];link=['-Wl,--stack,8388608']
    if a.x87_build:
        sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve()),str(Path(__file__).resolve().parents[1]/'tools')]
        from test_x87_environment import verify
        from aot_x87_extended import install
        core=json.loads(a.x87_build.read_text());verify(core)
        if core['switch_abi']:raise ValueError('Host x87 core required')
        objects=[Path(row['object']) for row in core['objects']]
        extra.extend([a.x87_build.resolve(),*objects,*[Path(n) for n in core['source_sha256']],Path(__file__).resolve().parents[1]/'tools/aot_x87_extended.py']);link.extend(str(o) for o in objects)
        a.cc.append('-DFNV_CPU_EXTENDED_X87=1');setup=install
    shared.NAMES=['Invoke'];shared.ARGS=[1];shared.DLLS=['callback-test.dll']
    shared.test(a,codegen_setup=setup,host_source='cpu_callbacks_host.c',guest_programs=programs,
        extra_inputs=extra,linker_options=link,
        strict_modes=('--strict-target','--strict-frame','--strict-ret','--strict-register','--strict-instruction','--strict-depth'))

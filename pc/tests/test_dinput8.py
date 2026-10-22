"""Translate DirectInput COM callers and verify ABI/layout/lifetime on Windows."""
import argparse
import struct
import json
import sys
from pathlib import Path
import test_windows_heap as shared
SLOTS=[(4,5),(0,3),(1,1),(2,1),(3,4),(5,2),(7,1),(8,1),(9,3),(11,2),(13,3),(5,3),(6,3),(10,5),(3,2)]
def programs(raw):
    raw.extend(bytes(12288-len(raw)))
    struct.pack_into('<I',raw,0x98+56,0x4000)
    struct.pack_into('<I',raw,0x98+224+8,len(raw)-512)
    struct.pack_into('<I',raw,0x98+224+16,len(raw)-512)
    out={}
    for i,(slot,argc) in enumerate(SLOTS):
        code=bytes.fromhex('8b4424048b08')+bytes((0xff,0x74,0x24,argc*4))*argc+bytes((0xff,0x51,slot*4))+b'\xc2'+struct.pack('<H',argc*4)
        out[0x402000+i*64]=code
    observe=struct.unpack_from('<I',raw,0x300+20+16)[0]+0x400000
    out[0x401c00]=bytes.fromhex('ff742408')*2+b'\xff\x15'+struct.pack('<I',observe)+bytes.fromhex('c20800')
    return out
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    p.add_argument('--x87-build',type=Path)
    a=p.parse_args();extra=[Path(__file__).resolve()];link=['-ldinput8','-ldxguid','-luser32','-Wl,--stack,8388608']
    if a.x87_build:
        sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve()),str(Path(__file__).resolve().parents[1]/'tools')]
        from test_x87_environment import verify
        core=json.loads(a.x87_build.read_text());verify(core)
        if core['switch_abi']:raise ValueError('Host extended core required')
        objects=[Path(o['object']) for o in core['objects']]
        extra.extend([a.x87_build.resolve(),*objects,*[Path(n) for n in core['source_sha256']]])
        link.extend(str(o) for o in objects);a.cc.append('-DFNV_CPU_EXTENDED_X87=1')
    shared.NAMES=['DirectInput8Create','Observe'];shared.ARGS=[5,2];shared.DLLS=['dinput8.dll','di-test.dll']
    shared.test(a,host_source='dinput8_host.c',guest_programs=programs,native_sources=('dinput8.c','dinput8_hid.c'),
        extra_inputs=(*extra,Path(__file__).resolve().parents[1]/'native/dinput8.h',Path(__file__).resolve().parents[1]/'native/dinput8_hid.h'),
        linker_options=link,
        strict_modes=('--strict-version','--strict-aggregation','--strict-stale','--strict-output','--strict-callback','--strict-foreground'))

"""Translated DirectSound8 callers, Windows ABI checks and exact PCM output."""
import argparse
import struct
import json
import sys
from pathlib import Path
import test_windows_heap as shared
SLOTS=[(0,3),(1,1),(2,1),(3,4),(4,2),(6,3),(11,2),(3,2),(4,3),(5,4),(9,2),(11,8),(12,4),(13,2),(14,2),(15,2),(16,2),(17,2),(18,1),(19,5),(20,1),(6,2),(7,2),(8,2)]
def programs(raw):
    raw.extend(bytes(16384-len(raw)))
    struct.pack_into('<I',raw,0x98+56,0x5000)
    struct.pack_into('<I',raw,0x98+224+8,len(raw)-512)
    struct.pack_into('<I',raw,0x98+224+16,len(raw)-512)
    lookup=struct.unpack_from('<I',raw,0x300+20)[0]
    struct.pack_into('<I',raw,lookup-0x1000+0x200,0x8000000b)
    out={}
    for i,(slot,argc) in enumerate(SLOTS):
        out[0x402000+i*64]=bytes.fromhex('8b4424048b08')+bytes((0xff,0x74,0x24,argc*4))*argc+bytes((0xff,0x51,slot*4))+b'\xc2'+struct.pack('<H',argc*4)
    return out
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);p.add_argument('--x87-build',type=Path)
    a=p.parse_args();pc=Path(__file__).resolve().parents[1];extra=[Path(__file__).resolve(),pc/'native/dsound8.h',pc/'native/pcm_queue.h'];link=['-ldsound','-ldxguid','-luser32','-lole32','-Wl,--stack,8388608']
    if a.x87_build:
        sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve()),str(pc/'tools')]
        from test_x87_environment import verify
        core=json.loads(a.x87_build.read_text());verify(core)
        if core['switch_abi']:raise ValueError('Host extended core required')
        objects=[Path(o['object']) for o in core['objects']]
        extra.extend([a.x87_build.resolve(),*objects,*[Path(n) for n in core['source_sha256']]])
        link.extend(str(o) for o in objects);a.cc.append('-DFNV_CPU_EXTENDED_X87=1')
    shared.NAMES=['DirectSoundCreate8','DirectSoundCreate8'];shared.ARGS=[3,3];shared.DLLS=['dsound.dll']*2
    shared.test(a,host_source='dsound8_host.c',guest_programs=programs,native_sources=('dsound8.c','pcm_queue.c'),extra_inputs=extra,linker_options=link,
        strict_modes=('--strict-output','--strict-stale','--strict-3d','--strict-format'))

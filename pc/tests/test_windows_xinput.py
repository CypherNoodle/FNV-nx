"""Translated ordinal XInput calls plus bounded/concurrent PCM tests."""
import argparse
from pathlib import Path
import struct
import test_windows_heap as shared


def ordinal_imports(raw):
    for i,ordinal in enumerate((2,3)):
        lookup=struct.unpack_from('<I',raw,0x300+i*20)[0]
        struct.pack_into('<I',raw,lookup-0x1000+0x200,0x80000000|ordinal)
    return {}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);p.add_argument('--arm-cc',type=Path)
    a=p.parse_args();shared.NAMES=['XInputGetState','XInputSetState'];shared.ARGS=[2,2];shared.DLLS=['XINPUT1_3.dll']*2
    pc=Path(__file__).resolve().parents[1]
    shared.test(a,host_source='windows_xinput_host.c',extra_inputs=(Path(__file__).resolve(),pc/'native/xinput.h',pc/'native/pcm_queue.h'),
        strict_modes=('--bad-output','--bad-vibration'),guest_programs=ordinal_imports,native_sources=('xinput.c','pcm_queue.c'))

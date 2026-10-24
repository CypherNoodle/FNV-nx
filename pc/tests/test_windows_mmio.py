"""Translated custom MMIO callbacks, buffered reads and RIFF against Windows."""
import argparse
from pathlib import Path
import struct
import test_windows_heap as shared

def programs(raw):
    iat=struct.unpack_from('<I',raw,0x300+9*20+16)[0]
    return {0x401d00:bytes((0xff,0x74,0x24,16))*4+b'\xff\x15'+struct.pack('<I',0x400000+iat)+b'\xc2\x10\x00'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['mmioOpenA','mmioClose','mmioRead','mmioSeek','mmioGetInfo','mmioSetInfo','mmioAdvance','mmioDescend','mmioAscend','TestIO']
    shared.ARGS=[3,2,3,3,3,3,3,4,3,4];shared.DLLS=['winmm.dll']*9+['test-mmio.dll']
    pc=Path(__file__).resolve().parents[1]
    shared.test(p.parse_args(),host_source='windows_mmio_host.c',guest_programs=programs,
        native_sources=('windows_mmio.c',),extra_inputs=(Path(__file__).resolve(),pc/'native/windows_mmio.h'),
        linker_options=('-Wl,--stack,16777216','-lwinmm'),
        strict_modes=('--strict-open-mode','--strict-output','--strict-callback-abi','--strict-set-info','--strict-end-offset'))

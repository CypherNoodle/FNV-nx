"""Translated thread-name event against a continuing Windows VEH observer."""
import argparse
import struct
from pathlib import Path
import test_windows_heap as shared
def programs(raw):
    iat=struct.unpack_from('<5I',raw,0x300+7*20)[4]+0x400000
    return {0x401c00:b'\xff\x74\x24\x04\xff\x15'+struct.pack('<I',iat)+b'\xc2\x04\x00'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['RaiseException','GetLastError','SetLastError','CreateThread','ResumeThread','WaitForSingleObject','CloseHandle','ObserveName'];shared.ARGS=[4,0,1,6,1,2,1,1];shared.DLLS=['kernel32.dll']*7+['fixture.dll']
    shared.test(p.parse_args(),host_source='windows_debug_host.c',native_sources=('windows_debug.c',),
        extra_inputs=(Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'native/windows_debug.h'),
        linker_options=('-Wl,--stack,8388608',),wrapper_stride=64,guest_programs=programs,strict_modes=('--strict-code','--strict-flags','--strict-count','--strict-type','--strict-record','--strict-text','--strict-id','--strict-encoding','--strict-length'))

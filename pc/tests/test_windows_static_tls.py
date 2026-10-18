"""Execute compiler-style x86 FS:2C/index TLS access on main and managed threads."""
import argparse
from pathlib import Path
import struct
import test_windows_heap as shared
import test_windows_workers as workers

def programs(raw):
    programs=workers.programs(raw)
    struct.pack_into('<II',raw,0x98+168,0x1b80,24)
    struct.pack_into('<I',raw,0x98+224+36,0xe0000020)
    struct.pack_into('<6I',raw,0xd80,0x401bb0,0x401bd0,0x401ba4,0x401ba0,24,0x500000)
    struct.pack_into('<II',raw,0xda0,0,0xffffffff)
    raw[0xdb0:0xdd0]=bytes.fromhex('44332211')+bytes(range(28))
    programs[0x401c00]=bytes.fromhex('8b4c24048b15a41b400064a12c0000008b04908b10891189410889088b50208951048bc1c20400')
    return programs

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['CreateThread','ExitThread','GetExitCodeThread','WaitForSingleObject','CloseHandle',
                  'GetCurrentThreadId','GetThreadId','GetCurrentThread','ResumeThread','Sleep',
                  'SetLastError','GetLastError','TlsSetValue','TlsGetValue','CreateMutexA',
                  'WaitForMultipleObjects','TlsAlloc']
    shared.ARGS=[6,1,2,2,1,0,1,0,1,1,1,0,2,1,3,4,0]
    shared.test(p.parse_args(),host_source='windows_static_tls_host.c',wrapper_stride=64,guest_programs=programs,
                extra_inputs=(Path(__file__).resolve(),Path(workers.__file__),Path(__file__).with_name('windows_workers_host.c')),
                strict_modes=('--strict-budget',))

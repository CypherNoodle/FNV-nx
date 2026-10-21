"""Execute translated SYSTEM_INFO32 imports in the local-console guest profile."""
import argparse
from pathlib import Path
import test_windows_heap as shared
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['GetSystemInfo','GetLastError','SetLastError']
    shared.ARGS=[1,0,1];shared.DLLS=['KERNEL32.dll']*3
    shared.test(p.parse_args(),host_source='windows_system_host.c',
                extra_inputs=(Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'native/windows_system.h'),
                native_sources=('windows_system.c',),strict_modes=('--strict-null','--strict-boundary','--strict-readonly','--strict-uncommitted'))

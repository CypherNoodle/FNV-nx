"""Execute translated USER32 imports in the local-console guest profile."""
import argparse
from pathlib import Path
import test_windows_heap as shared
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['GetSystemMetrics','GetLastError','SetLastError','FindWindowA','GetUserNameA']
    shared.ARGS=[1,0,1,2,2];shared.DLLS=['USER32.dll','KERNEL32.dll','KERNEL32.dll','USER32.dll','ADVAPI32.dll']
    shared.test(p.parse_args(),host_source='windows_user32_host.c',
                extra_inputs=(Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'native/windows_user32.h'),
                native_sources=('windows_user32.c',),strict_modes=('--strict-width','--strict-height','--strict-unknown','--strict-negative','--strict-find-pointer','--strict-find-atom','--strict-find-encoding','--strict-find-length','--strict-username-size','--strict-username-output'))

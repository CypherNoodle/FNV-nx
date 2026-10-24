"""Compare translated FILETIME calls with native Windows, including high-bit times."""
import argparse
from pathlib import Path
import test_windows_heap as shared
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['CompareFileTime','GetLastError','SetLastError'];shared.ARGS=[2,0,1];shared.DLLS=['kernel32.dll']*3
    shared.test(p.parse_args(),host_source='windows_filetime_host.c',native_sources=('windows_filetime.c',),extra_inputs=(Path(__file__),Path(__file__).resolve().parents[1]/'native/windows_filetime.h'),linker_options=('-Wl,--stack,8388608',),strict_modes=('--strict-null','--strict-uncommitted'))

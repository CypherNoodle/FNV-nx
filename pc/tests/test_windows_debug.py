"""Translated thread-name event against a continuing Windows VEH observer."""
import argparse
from pathlib import Path
import test_windows_heap as shared
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['RaiseException','GetLastError','SetLastError'];shared.ARGS=[4,0,1];shared.DLLS=['kernel32.dll']*3
    shared.test(p.parse_args(),host_source='windows_debug_host.c',native_sources=('windows_debug.c',),
        extra_inputs=(Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'native/windows_debug.h'),
        linker_options=('-Wl,--stack,8388608',),strict_modes=('--strict-code','--strict-flags','--strict-count','--strict-type','--strict-record','--strict-text','--strict-id','--strict-encoding','--strict-length'))

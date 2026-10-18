"""Translated process-parameter calls and Shell32 parser differential tests."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['GetCommandLineA','GetCommandLineW','CommandLineToArgvW','LocalFree','GetLastError','SetLastError','GetProcessHeap','HeapSize','HeapAlloc','HeapFree','HeapReAlloc']
    shared.ARGS=[0,0,2,1,0,1,0,3,3,3,4]
    shared.DLLS=['shell32.dll' if n=='CommandLineToArgvW' else 'kernel32.dll' for n in shared.NAMES]
    shared.test(p.parse_args(),host_source='windows_parameters_host.c',
        extra_inputs=(Path(__file__).resolve(),),
        strict_modes=('--strict-input','--strict-count','--strict-wrap','--strict-unavailable'))

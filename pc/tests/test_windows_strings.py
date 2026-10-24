"""Translated bounded lstrcpyA bridge against native Windows byte-string copies."""
import argparse
from pathlib import Path
import test_windows_heap as shared
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['lstrcpyA','GetLastError','SetLastError','lstrcatA'];shared.ARGS=[2,0,1,2];shared.DLLS=['kernel32.dll']*4
    shared.test(p.parse_args(),host_source='windows_strings_host.c',native_sources=('windows_strings.c',),extra_inputs=(Path(__file__),Path(__file__).resolve().parents[1]/'native/windows_strings.h',Path(__file__).resolve().parents[1]/'native/windows_system.h'),linker_options=('-Wl,--stack,8388608',),strict_modes=('--strict-overlap','--strict-limit'))

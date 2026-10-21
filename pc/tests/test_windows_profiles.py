"""Differential x86 INI integer calls against real Windows profile APIs."""
import argparse
from pathlib import Path
import test_windows_heap as shared
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['GetPrivateProfileIntA','GetPrivateProfileIntW','GetLastError','SetLastError']
    shared.ARGS=[4,4,0,1]
    shared.test(p.parse_args(),host_source='windows_profiles_host.c',
                extra_inputs=(Path(__file__).resolve(),*[Path(__file__).resolve().parents[1]/'native'/n for n in ('windows_profiles.h','windows_profiles.inc','windows_system.h')]),
                strict_modes=('--strict-traversal','--strict-absolute','--strict-encoding','--strict-pointer','--strict-long-line'))

"""Differential translated UTF-16 conversion against Windows code pages."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--arm-cc',type=Path);p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['WideCharToMultiByte','GetLastError','SetLastError','GetACP','IsValidCodePage','GetCPInfo']
    shared.ARGS=[8,0,1,0,1,2]
    shared.test(p.parse_args(),host_source='windows_charset_host.c',extra_inputs=(Path(__file__).resolve(),),wrapper_stride=64,
                strict_modes=('--strict-bestfit','--strict-codepage','--strict-flags','--strict-pointer'))

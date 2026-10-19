"""Western CTYPE1 and byte-to-UTF16 comparisons against Windows."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--arm-cc',type=Path);p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['GetStringTypeW','MultiByteToWideChar','LCMapStringW','GetLastError','SetLastError']
    shared.ARGS=[4,6,6,0,1]
    shared.test(p.parse_args(),host_source='windows_nls_host.c',extra_inputs=(Path(__file__).resolve(),),wrapper_stride=64,
        strict_modes=('--strict-classification','--strict-level','--strict-decode-flags','--strict-pointer','--strict-map-locale','--strict-map-flags'))

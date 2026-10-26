"""Compare virtual GetFullPathName A/W through translated callers with Windows."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['GetFullPathNameA','GetFullPathNameW','GetLastError','SetLastError']
    shared.ARGS=[4,4,0,1]
    shared.test(p.parse_args(),host_source='windows_full_path_host.c',wrapper_stride=64,
        extra_inputs=(Path(__file__).resolve(),),
        strict_modes=('--strict-output','--strict-part','--strict-alias','--strict-input'),
        linker_options=('-Wl,--stack,8388608',))

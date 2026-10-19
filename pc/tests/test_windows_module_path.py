"""Main-image GetModuleFileName A/W snapshots and Windows truncation oracle."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--arm-cc',type=Path);p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['GetModuleFileNameA','GetModuleFileNameW','GetLastError','SetLastError']
    shared.ARGS=[3,3,0,1]
    shared.test(p.parse_args(),host_source='windows_module_path_host.c',extra_inputs=(Path(__file__).resolve(),),
                strict_modes=('--strict-pointer','--strict-parameters'))

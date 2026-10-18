"""Translated virtual environment snapshots, ownership and bounded allocation."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--arm-cc',type=Path);p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['GetEnvironmentStringsA','GetEnvironmentStringsW','FreeEnvironmentStringsA','FreeEnvironmentStringsW',
                  'GetLastError','SetLastError','GetProcessHeap','HeapAlloc','HeapFree','LocalFree']
    shared.ARGS=[0,0,1,1,0,1,0,3,3,1]
    shared.test(p.parse_args(),host_source='windows_environment_host.c',extra_inputs=(Path(__file__).resolve(),),strict_modes=())

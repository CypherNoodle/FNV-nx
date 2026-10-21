"""Compare translated empty-registry queries with absent Windows keys."""
import argparse
from pathlib import Path
import test_windows_heap as shared
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['RegOpenKeyExA','RegCloseKey'];shared.ARGS=[5,1];shared.DLLS=['advapi32.dll']*2
    shared.test(p.parse_args(),host_source='windows_registry_host.c',wrapper_stride=64,
        native_sources=('windows_registry.c',),
        extra_inputs=(Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'native/windows_registry.h'),
        strict_modes=('--strict-root','--strict-options','--strict-access','--strict-output','--strict-input','--strict-encoding','--strict-length'))

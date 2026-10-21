"""Execute a translated CPUID instruction against the bounded guest contract."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['GetLastError'];shared.ARGS=[0]
    shared.test(p.parse_args(),host_source='guest_cpuid_host.c',
        extra_inputs=(Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'native/guest_cpuid.h'),
        guest_programs=lambda raw:{0x401800:bytes.fromhex('0fa2c3')},
        strict_modes=('--strict-basic','--strict-extended','--strict-brand'))

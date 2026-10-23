"""Translated x86 QPC/QPF calls, checked against bracketing host timestamps."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['QueryPerformanceCounter','QueryPerformanceFrequency','Sleep','GetLastError','SetLastError','GetTickCount','GetSystemTimeAsFileTime','GetStartupInfoA','GetStartupInfoW']
    shared.ARGS=[1,1,1,0,1,0,1,1,1]
    shared.test(p.parse_args(),host_source='windows_time_host.c',extra_inputs=(Path(__file__).resolve(),),
                strict_modes=('--strict-counter','--strict-frequency','--strict-clock-backwards','--strict-clock-failure','--strict-filetime','--strict-startup-info'),
                linker_options=('-Wl,--stack,8388608',))

"""Unnamed events and generation handles through SDK-translated guest IAT calls."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'python-deps', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--cc', nargs='+', required=True)
    shared.NAMES = ['CreateEventA', 'SetEvent', 'ResetEvent', 'WaitForSingleObject',
                    'CloseHandle', 'CreateEventW', 'GetLastError', 'SetLastError']
    shared.ARGS = [4, 1, 1, 2, 1, 4, 0, 1]
    shared.test(p.parse_args(), host_source='windows_events_host.c',
                extra_inputs=(Path(__file__).resolve(),),
                strict_modes=('--strict-name', '--strict-security', '--strict-disabled', '--strict-peer'))

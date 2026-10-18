"""Critical sections and Interlocked through actual SDK-translated IAT calls."""
import argparse
from pathlib import Path
import test_windows_heap as shared


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'python-deps', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--cc', nargs='+', required=True)
    shared.NAMES = ['InitializeCriticalSection', 'InitializeCriticalSectionAndSpinCount',
                    'EnterCriticalSection', 'TryEnterCriticalSection', 'LeaveCriticalSection',
                    'DeleteCriticalSection', 'SetCriticalSectionSpinCount', 'InterlockedExchange',
                    'InterlockedCompareExchange', 'InterlockedIncrement', 'InterlockedDecrement',
                    'InterlockedExchangeAdd', 'GetLastError', 'SetLastError']
    shared.ARGS = [1, 2, 1, 1, 1, 1, 2, 2, 3, 1, 1, 2, 0, 1]
    shared.test(p.parse_args(), host_source='windows_sync_host.c',
                extra_inputs=(Path(__file__).resolve(),),
                strict_modes=('--strict-uninitialized', '--strict-nonowner',
                              '--strict-modified', '--strict-delete', '--strict-capacity', '--strict-peer'))

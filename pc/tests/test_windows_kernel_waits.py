"""Compare translated x86 semaphore/mutex/multiwait imports with Windows."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'python-deps', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--cc', nargs='+', required=True)
    shared.NAMES = ['CreateEventA', 'SetEvent', 'ResetEvent', 'WaitForSingleObject',
                    'CloseHandle', 'CreateEventW', 'GetLastError', 'SetLastError',
                    'CreateSemaphoreA', 'ReleaseSemaphore', 'CreateMutexA',
                    'ReleaseMutex', 'WaitForMultipleObjects', 'CreateSemaphoreW', 'CreateMutexW']
    shared.ARGS = [4, 1, 1, 2, 1, 4, 0, 1, 4, 3, 3, 1, 4, 4, 3]
    shared.test(p.parse_args(), host_source='windows_kernel_waits_host.c',
                extra_inputs=(Path(__file__).resolve(), Path(__file__).with_name('windows_events_host.c')),
                strict_modes=('--strict-name', '--strict-security', '--strict-disabled', '--strict-peer'))

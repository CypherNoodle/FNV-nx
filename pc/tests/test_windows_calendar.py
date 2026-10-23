"""Compare translated FILETIME imports with Windows calendar/timezone APIs."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'python-deps', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--cc', nargs='+', required=True)
    p.add_argument('--arm-cc', type=Path)
    shared.NAMES = ['FileTimeToLocalFileTime', 'FileTimeToSystemTime', 'GetLastError', 'SetLastError', 'GetTimeZoneInformation']
    shared.ARGS = [2, 2, 0, 1, 1]
    pc = Path(__file__).resolve().parents[1]
    shared.test(p.parse_args(), host_source='windows_calendar_host.c',
                extra_inputs=(Path(__file__).resolve(), pc/'tests/windows_files_host.c',
                              pc/'tests/timezone_rule_test.inc', pc/'native/host_calendar.h', pc/'native/windows_calendar.inc'),
                wrapper_stride=64, strict_modes=('--strict-input', '--strict-output', '--strict-zone-output'),
                linker_options=('-Wl,--stack,8388608',))

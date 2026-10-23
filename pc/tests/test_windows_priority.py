"""Translated thread-priority ABI compared with real Windows priorities."""
import argparse
from pathlib import Path
import test_windows_heap as shared

def programs(raw):
    return {0x401c00: bytes.fromhex('31c0c20400')}

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'python-deps', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--cc', nargs='+', required=True)
    shared.NAMES = ['CreateThread', 'ResumeThread', 'WaitForSingleObject',
                    'CloseHandle', 'SetThreadPriority', 'GetThreadPriority',
                    'GetLastError', 'SetLastError']
    shared.ARGS = [6, 1, 2, 1, 2, 1, 0, 1]
    shared.test(p.parse_args(), host_source='windows_priority_host.c',
                extra_inputs=(Path(__file__).resolve(),), wrapper_stride=64,
                guest_programs=programs, strict_modes=(),
                linker_options=('-Wl,--stack,8388608',))

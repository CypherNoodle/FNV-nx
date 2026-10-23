"""SECURITY_ATTRIBUTES32 through translated ANSI/wide file calls."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'python-deps', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cc', nargs='+', required=True)
    parser.add_argument('--arm-cc', type=Path)
    args = parser.parse_args()
    shared.NAMES = ['CreateFileA', 'CreateFileW', 'ReadFile', 'GetFileSize',
                    'GetFileSizeEx', 'SetFilePointer', 'SetFilePointerEx',
                    'CloseHandle', 'GetLastError', 'SetLastError', 'CreateEventA']
    shared.ARGS = [7, 7, 5, 2, 2, 4, 5, 1, 0, 1, 4]
    pc = Path(__file__).resolve().parents[1]
    shared.test(args, host_source='windows_file_security_host.c',
                extra_inputs=(Path(__file__).resolve(), pc/'tests/windows_files_host.c'),
                wrapper_stride=64, strict_modes=('--strict-security',),
                linker_options=('-Wl,--stack,8388608',))

"""Virtual current-directory buffer ABI and file-root round trips."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'python-deps', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--cc', nargs='+', required=True)
    parser.add_argument('--arm-cc', type=Path)
    shared.NAMES = ['GetCurrentDirectoryA', 'GetCurrentDirectoryW', 'CreateFileA',
                    'GetFileAttributesA', 'ReadFile', 'CloseHandle',
                    'GetLastError', 'SetLastError']
    shared.ARGS = [2, 2, 7, 1, 5, 1, 0, 1]
    pc = Path(__file__).resolve().parents[1]
    shared.test(parser.parse_args(), host_source='windows_current_directory_host.c',
                extra_inputs=(Path(__file__).resolve(), pc/'tests/windows_files_host.c'),
                wrapper_stride=64, strict_modes=('--strict-output',),
                linker_options=('-Wl,--stack,8388608',))

"""Compare translated legacy DDS metadata calls against native Windows D3DX."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'python-deps', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--cc', nargs='+', required=True)
    p.add_argument('--arm-cc', type=Path)
    shared.NAMES = ['D3DXGetImageInfoFromFileInMemory', 'GetLastError', 'SetLastError']
    shared.ARGS = [3, 0, 1]
    shared.DLLS = ['d3dx9_38.dll', 'kernel32.dll', 'kernel32.dll']
    pc = Path(__file__).resolve().parents[1]
    shared.test(p.parse_args(), host_source='d3dx_image_info_host.c', native_sources=('windows_d3dx.c',),
                extra_inputs=(Path(__file__).resolve(), pc/'tests/windows_files_host.c', pc/'native/dds_image_info.h',
                              pc/'native/windows_d3dx.h', Path('C:/Windows/System32/d3dx9_38.dll')),
                wrapper_stride=64, strict_modes=('--strict-input', '--strict-output', '--strict-format'),
                linker_options=('-Wl,--stack,8388608',))

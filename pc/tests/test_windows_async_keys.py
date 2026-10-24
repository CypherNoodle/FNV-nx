"""Compare translated GetAsyncKeyState virtual snapshots against Windows USER32."""
import argparse
from pathlib import Path
import test_windows_heap as shared
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['GetAsyncKeyState','GetLastError','SetLastError']
    shared.ARGS=[1,0,1];shared.DLLS=['USER32.dll','KERNEL32.dll','KERNEL32.dll']
    shared.test(p.parse_args(),host_source='windows_async_keys_host.c',
        extra_inputs=(Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'native/windows_user32.h',Path(__file__).resolve().parents[1]/'native/windows_user32_input.h',Path(__file__).resolve().parents[1]/'native/dinput8.h'),
        native_sources=('windows_user32.c','windows_user32_input.c'),linker_options=('-Wl,--stack,16777216',),
        strict_modes=('--strict-uninitialized',))

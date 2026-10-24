"""Compare translated PeekMessageA thread queues against Windows USER32."""
import argparse
from pathlib import Path
import test_windows_heap as shared
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['PeekMessageA','GetLastError','SetLastError']
    shared.ARGS=[5,0,1];shared.DLLS=['USER32.dll','KERNEL32.dll','KERNEL32.dll']
    shared.test(p.parse_args(),host_source='windows_messages_host.c',
        extra_inputs=(Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'native/windows_user32.h'),
        native_sources=('windows_user32.c',),linker_options=('-Wl,--stack,16777216',),
        strict_modes=('--strict-output','--strict-flags','--strict-window','--strict-filter'))

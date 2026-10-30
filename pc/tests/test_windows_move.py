"""Translated save-file rename, compared with Windows without touching saves."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['MoveFileA','GetLastError','SetLastError','CreateFileA','CloseHandle']
    shared.ARGS=[2,0,1,7,1]
    pc=Path(__file__).resolve().parents[1]
    shared.test(p.parse_args(),host_source='windows_move_host.c',wrapper_stride=64,strict_modes=(),
        extra_inputs=(Path(__file__).resolve(),pc/'tests/windows_files_host.c'),linker_options=('-Wl,--stack,8388608',))

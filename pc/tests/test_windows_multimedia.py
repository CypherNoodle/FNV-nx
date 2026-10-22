"""Translated timeGetTime compared with Windows and the shared guest uptime."""
import argparse
from pathlib import Path
import test_windows_heap as shared
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['timeGetTime','GetTickCount','GetLastError','SetLastError'];shared.ARGS=[0,0,0,1]
    shared.DLLS=['winmm.dll','kernel32.dll','kernel32.dll','kernel32.dll']
    shared.test(p.parse_args(),host_source='windows_multimedia_host.c',native_sources=('windows_multimedia.c',),
        extra_inputs=(Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'native/windows_multimedia.h'),
        linker_options=('-Wl,--stack,8388608','-lwinmm'),strict_modes=())

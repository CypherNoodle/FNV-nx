"""Translate display enumeration and compare packed records with Windows."""
import argparse
import json
import sys
from pathlib import Path
import test_windows_heap as shared
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    p.add_argument('--x87-build',type=Path)
    a=p.parse_args();pc=Path(__file__).resolve().parents[1]
    extra=[Path(__file__).resolve(),pc/'native/windows_display.h',pc/'native/windows_display_win32.h',pc/'native/windows_user32.h',pc/'native/windows_display_resolution.h'];link=['-Wl,--stack,8388608','-luser32']
    if a.x87_build:
        sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve()),str(pc/'tools')]
        from test_x87_environment import verify
        core=json.loads(a.x87_build.read_text());verify(core)
        if core['switch_abi']:raise ValueError('Host extended core required')
        objects=[Path(o['object']) for o in core['objects']]
        extra.extend([a.x87_build.resolve(),*objects,*[Path(n) for n in core['source_sha256']]])
        link.extend(str(o) for o in objects);a.cc.append('-DFNV_CPU_EXTENDED_X87=1')
    shared.NAMES=['EnumDisplayDevicesA','GetLastError','SetLastError','GetSystemMetrics'];shared.ARGS=[4,0,1,1]
    shared.DLLS=['user32.dll','kernel32.dll','kernel32.dll','user32.dll']
    shared.test(a,host_source='windows_display_host.c',native_sources=('windows_display.c','windows_user32.c','windows_display_resolution.c'),
        extra_inputs=extra,
        linker_options=link,
        strict_modes=('--strict-output','--strict-parent','--strict-parent-long','--strict-flags','--strict-no-backend','--strict-backend-error','--strict-record','--strict-size'))

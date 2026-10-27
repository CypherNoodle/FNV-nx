"""Compare translated per-thread COM initialization with native Windows."""
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
    a=p.parse_args();extra=[Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'native/windows_com.h',Path(__file__).resolve().parents[1]/'native/windows_com_diag.h',Path(__file__).resolve().parents[1]/'native/windows_com_classes.h']
    link=['-Wl,--stack,8388608','-lole32']
    if a.x87_build:
        sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve()),str(Path(__file__).resolve().parents[1]/'tools')]
        from test_x87_environment import verify
        core=json.loads(a.x87_build.read_text());verify(core)
        if core['switch_abi']:raise ValueError('Host extended core required')
        objects=[Path(row['object']) for row in core['objects']]
        extra.extend([a.x87_build.resolve(),*objects,*[Path(n) for n in core['source_sha256']]])
        link.extend(str(o) for o in objects);a.cc.append('-DFNV_CPU_EXTENDED_X87=1')
    shared.NAMES=['CoInitialize','CoInitializeEx','CoUninitialize','GetLastError','SetLastError','CoCreateInstance']
    shared.ARGS=[1,2,0,0,1,5];shared.DLLS=['ole32.dll']*3+['kernel32.dll']*2+['ole32.dll']
    shared.test(a,host_source='windows_com_host.c',native_sources=('windows_com.c',),
        extra_inputs=extra,
        linker_options=link,strict_modes=())

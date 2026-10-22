"""Guest CreateWindow lifecycle with real Win32 backing and message oracle."""
import argparse
from pathlib import Path
import struct
import json
import sys
import test_windows_heap as shared

def programs(raw):
    observe=struct.unpack_from('<I',raw,0x300+5*20+16)[0]+0x400000
    return {0x401c00:bytes.fromhex('ff742410')*4+b'\xff\x15'+struct.pack('<I',observe)+bytes.fromhex('c21000')}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    p.add_argument('--x87-build',type=Path)
    a=p.parse_args();extra=[Path(__file__).resolve()];link=['-luser32','-lgdi32','-Wl,--stack,8388608']
    if a.x87_build:
        sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve()),str(Path(__file__).resolve().parents[1]/'tools')]
        from test_x87_environment import verify
        core=json.loads(a.x87_build.read_text());verify(core)
        if core['switch_abi']:raise ValueError('Host extended core required')
        objects=[Path(row['object']) for row in core['objects']]
        extra.extend([a.x87_build.resolve(),*objects,*[Path(n) for n in core['source_sha256']]])
        link.extend(str(o) for o in objects);a.cc.append('-DFNV_CPU_EXTENDED_X87=1')
    shared.NAMES=['RegisterClassA','CreateWindowExA','DestroyWindow','UnregisterClassA','GetLastError','Observe','FindWindowA','DefWindowProcA','GetActiveWindow','SwapMouseButton','GetSystemMetrics','GetDoubleClickTime','SetDoubleClickTime','GetClientRect']
    shared.ARGS=[1,12,1,2,0,4,2,4,0,1,1,0,1,2]
    shared.DLLS=['user32.dll']*4+['kernel32.dll','test-ui.dll','user32.dll','user32.dll','user32.dll','user32.dll','user32.dll','user32.dll','user32.dll','user32.dll']
    shared.test(a,host_source='windows_create_host.c',wrapper_stride=96,guest_programs=programs,
        native_sources=('windows_boot_ui.c','pe_resources.c','windows_user32.c'),
        extra_inputs=(*extra,*[Path(__file__).resolve().parents[1]/'native'/n for n in ('windows_boot_ui.h','pe_resources.h','windows_user32.h')]),
        linker_options=link,
        strict_modes=('--strict-no-backend','--strict-style','--strict-stack'))

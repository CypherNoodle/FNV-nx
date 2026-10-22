"""Native module catalog reached through translated x86 imports and indirect calls."""
import argparse
from pathlib import Path
import test_windows_heap as shared

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    p.add_argument('--trace',action='store_true')
    a=p.parse_args()
    if a.trace:a.cc.append('-DFNV_MODULE_TRACE=1')
    shared.NAMES=['LoadLibraryA','LoadLibraryW','GetModuleHandleA','GetModuleHandleW','GetProcAddress','FreeLibrary','GetLastError','SetLastError','GetLastError']
    shared.ARGS=[1,1,1,1,2,1,0,1,0]
    # Dynamic proc pointer comes from guest argument; call it through EAX.
    def programs(raw):return {0x401800:bytes.fromhex('8b442404ffd0c20400')}
    shared.test(a,host_source='windows_modules_host.c',
        extra_inputs=(Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'native/windows_modules.h'),
        native_sources=('windows_modules.c',),guest_programs=programs,linker_options=('-Wl,--stack,8388608',),
        strict_modes=('--strict-pointer','--strict-wide-wrap','--strict-provider','--strict-vm','--strict-capacity'))

"""Translate synthetic cdecl Steam callback callers; no Steam client needed."""
import argparse
from pathlib import Path
import struct
import test_windows_heap as shared

def callers(raw):
    result={}
    for i,argc in enumerate((2,1,0)):
        iat=struct.unpack_from('<I',raw,0x300+i*20+16)[0]
        # Forward parameters, then perform cdecl cleanup before wrapper return.
        code=bytes((0xff,0x74,0x24,argc*4))*argc
        code+=b'\xff\x15'+struct.pack('<I',0x400000+iat)
        code+=b'\x83\xc4'+bytes((argc*4,))+b'\xc2'+struct.pack('<H',argc*4)
        result[0x401500+i*32]=code
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['SteamAPI_RegisterCallback','SteamAPI_UnregisterCallback','SteamAPI_Init']
    shared.ARGS=[2,1,0];shared.DLLS=['steam_api.dll']*3
    shared.test(p.parse_args(),host_source='steam_callbacks_host.c',guest_programs=callers,
        native_sources=('steam_callbacks.c',),
        linker_options=('-Wl,--stack,8388608',),
        extra_inputs=(Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'native/steam_callbacks.h'),
        strict_modes=('--strict-null','--strict-range','--strict-flags','--strict-unowned','--strict-rebind','--strict-capacity'))

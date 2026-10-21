"""Exercise translated resource/class calls with an original synthetic icon."""
import argparse
import struct
from pathlib import Path
import test_windows_heap as shared


def programs(raw):
    raw.extend(bytes(12800-len(raw)))
    # Keep code and resources in separate sections; all payloads are synthetic.
    struct.pack_into('<I', raw, 0x98+56, 0x4000)
    section=0x98+224
    struct.pack_into('<I', raw, section+8, 0x1000)
    struct.pack_into('<I', raw, section+16, 0x1000)
    struct.pack_into('<II', raw, 0x98+112, 0x3000, 0x100)
    struct.pack_into('<H',raw,0x86,2)
    second=section+40
    raw[second:second+8]=b'.rsrc\0\0\0'
    struct.pack_into('<IIII',raw,second+8,0x1000,0x3000,0x1000,0x2200)
    struct.pack_into('<I',raw,second+36,0x40000040)
    tree=bytearray(256)
    for offset,count in ((0,2),(32,1),(56,1),(80,1),(104,1)):
        struct.pack_into('<H',tree,offset+14,count)
    for offset,ident,target in ((16,3,0x80000020),(24,14,0x80000038),
                                (48,1,0x80000050),(72,101,0x80000068),
                                (96,0x409,128),(120,0x409,144)):
        struct.pack_into('<II',tree,offset,ident,target)
    struct.pack_into('<IIII',tree,128,0x30b8,48,0,0)
    struct.pack_into('<IIII',tree,144,0x30a0,20,0,0)
    struct.pack_into('<HHHBBBBHHIH',tree,160,0,1,1,1,1,0,0,1,32,48,1)
    struct.pack_into('<IiiHHIIiiII',tree,184,40,1,2,1,32,0,8,0,0,0,0)
    tree[224:228]=bytes((0,127,255,255))
    raw[0x2200:0x2300]=tree
    return {0x401f00:bytes.fromhex('b801000000c21000')}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['LoadIconA','GetStockObject','RegisterClassA','UnregisterClassA','AdjustWindowRect','GetLastError','SetLastError']
    shared.ARGS=[2,1,1,2,3,0,1]
    shared.DLLS=['user32.dll','gdi32.dll','user32.dll','user32.dll','user32.dll','kernel32.dll','kernel32.dll']
    shared.test(p.parse_args(),host_source='windows_boot_ui_host.c',wrapper_stride=64,
        guest_programs=programs,native_sources=('windows_boot_ui.c','pe_resources.c','windows_user32.c'),
        extra_inputs=(Path(__file__).resolve(),*[Path(__file__).resolve().parents[1]/'native'/n for n in ('windows_boot_ui.h','pe_resources.h')]),
        linker_options=('-luser32','-lgdi32','-Wl,--stack,8388608'),
        strict_modes=('--strict-icon-name','--strict-icon-module','--strict-stock','--strict-class-pointer','--strict-class-procedure','--strict-class-extra','--strict-empty-name','--strict-rectangle','--strict-resource'))

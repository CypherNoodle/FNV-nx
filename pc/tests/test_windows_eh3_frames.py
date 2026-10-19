"""Nested normal EH3 cookie frames with actual translated calls and returns."""
import argparse
from pathlib import Path
import struct
import sys
import test_windows_heap as shared


PREFIX=bytes.fromhex('5064ff35000000008d44240c2b64240c53565789288be8')


def programs(raw):
    def call(at,target):return b'\xe8'+struct.pack('<i',target-at-5)
    def procedure(at,nested=False):
        code=bytearray()
        if not nested:code.extend(bytes.fromhex('648b3d24000000c1e70581c700904000'))
        code.extend(b'\x6a\x20\xb8'+struct.pack('<I',0x4019e0));code.extend(call(at+len(code),0x401900))
        code.extend(bytes.fromhex('64a100000000'));code.extend(bytes.fromhex('89470c' if nested else '8907'))
        code.extend(bytes.fromhex('8b500c895718' if nested else '8b500c895714'))
        code.extend(bytes.fromhex('8b00894710' if nested else '8b00894704'))
        code.extend(bytes.fromhex('bb10325476be78563412'))
        if not nested:code.extend(call(at+len(code),0x401880));code.extend(bytes.fromhex('64a100000000894708'))
        code.extend(call(at+len(code),0x401960));code.extend(b'\xc3');return bytes(code)
    prolog=PREFIX+b'\xa1'+struct.pack('<I',0x401a20)+bytes.fromhex('33c550ff75fcc745fcffffffff8d45f464a300000000c3')
    epilog=bytes.fromhex('8b4df464890d00000000595f5f5e5b8be55d51c3')
    struct.pack_into('<I',raw,0x401a20-0x401000+0x200,0xa15c3e79)
    return {0x401800:procedure(0x401800),0x401880:procedure(0x401880,True),0x401900:prolog,0x401960:epilog,0x4019e0:b'\xc3'}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);p.add_argument('--arm-cc',type=Path);a=p.parse_args()
    tools=Path(__file__).resolve().parents[1]/'tools';sys.path.insert(0,str(tools))
    def setup():
        from aot_frame_restore import install as frame
        from aot_eh3_frames import install
        frame();report=dict(abi='msvc-eh3-cookie-normal-frame-v1',helpers=[dict(address=0x401900,publish_address=0x401900+len(PREFIX)-2),dict(address=0x401960)])
        install(report);install(report)
    shared.NAMES=['GetLastError','SetLastError'];shared.ARGS=[0,1]
    shared.test(a,host_source='windows_eh3_frames_host.c',extra_inputs=(Path(__file__).resolve(),tools/'aot_frame_restore.py',tools/'aot_eh3_frames.py',
        Path(__file__).with_name('windows_seh_cookie_host.c')),strict_modes=(),guest_programs=programs,codegen_setup=setup,
        translator_options={'seh_prolog':0,'seh_epilog':0})

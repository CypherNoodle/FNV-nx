"""Normal nested x86 SEH registration frames, not exception dispatch/unwind."""
import argparse
from pathlib import Path
import struct
import sys
import test_windows_heap as shared


USE_LEAVE=False
USE_LEA=False
def programs(raw):
    def call(at, target):return b'\xe8'+struct.pack('<i', target-at-5)
    def procedure(at, nested=False):
        code=bytearray()
        def emit(value):code.extend(value)
        def invoke(target):emit(call(at+len(code), target))
        if not nested:
            # Own per-thread output: 0x409000 + thread ID * 32.
            emit(bytes.fromhex('648b3d24000000c1e70581c700904000'))
        emit(b'\x6a\x20\x68'+struct.pack('<I', 0x4019e0));invoke(0x401900)
        emit(bytes.fromhex('64a100000000'))
        emit(bytes.fromhex('89470c' if nested else '8907'))
        emit(bytes.fromhex('8b00894710' if nested else '8b00894704'))
        if not nested:
            invoke(0x401880);emit(bytes.fromhex('64a100000000894708'))
        invoke(0x401960);emit(b'\xc3')
        return bytes(code)
    return {
        0x401800:procedure(0x401800),0x401880:procedure(0x401880, True),
        # Helper consumes its arguments and returns with a newly installed
        # registration still on the guest stack. This exercises nonstandard
        # stack transitions across actual translated CALL/RET boundaries.
        0x401900:bytes.fromhex('595a58558d2c2429c45264ff35000000006489250000000051c3' if USE_LEA else '595a585589e529c45264ff35000000006489250000000051c3'),
        0x401960:bytes.fromhex('595864a30000000083c404c951c3' if USE_LEAVE else '595864a30000000083c40489ec5d51c3'),
        0x4019e0:b'\xc3',
    }


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    p.add_argument('--arm-cc',type=Path,help='Also compile the same generated fixture for Switch ARM64')
    p.add_argument('--without-overlay',action='store_true',help='Negative control: the known SDK frame bug must fail')
    p.add_argument('--leave',action='store_true',help='Use LEAVE instead of MOV ESP,EBP / POP EBP')
    p.add_argument('--lea',action='store_true',help='Establish helper frame using LEA EBP,[ESP]')
    a=p.parse_args();overlay=Path(__file__).resolve().parents[1]/'tools/aot_frame_restore.py'
    USE_LEAVE=a.leave
    USE_LEA=a.lea
    sys.path.insert(0,str(overlay.parent))
    from aot_frame_restore import install
    shared.NAMES=['GetLastError','SetLastError'];shared.ARGS=[0,1]
    shared.test(a,host_source='windows_seh_frames_host.c',
        extra_inputs=(Path(__file__).resolve(),overlay),strict_modes=(),guest_programs=programs,
        codegen_setup=None if a.without_overlay else install,
        translator_options={'seh_prolog':0x401900,'seh_epilog':0x401960})

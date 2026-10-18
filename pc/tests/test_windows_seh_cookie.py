"""Execute own nested cookie-registration helpers via the x86 AOT runtime."""
import argparse
from pathlib import Path
import struct
import sys
import test_windows_heap as shared
import test_windows_seh_frames as frames
from seh_cookie_fixture import helpers


def programs(raw):
    result=frames.programs(raw)
    prolog,epilog=helpers(0x4019e0,0x401a20)
    result.update({0x401900:prolog,0x401960:epilog})
    # Writable fixture data, not translated as a function.
    struct.pack_into('<I',raw,0x401a20-0x401000+0x200,0xa15c3e79)
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);p.add_argument('--arm-cc',type=Path)
    p.add_argument('--without-overlay',action='store_true')
    a=p.parse_args();overlay=Path(__file__).resolve().parents[1]/'tools/aot_frame_restore.py'
    sys.path.insert(0,str(overlay.parent));from aot_frame_restore import install
    shared.NAMES=['GetLastError','SetLastError'];shared.ARGS=[0,1]
    shared.test(a,host_source='windows_seh_cookie_host.c',
        extra_inputs=(Path(__file__).resolve(),Path(frames.__file__),Path(__file__).with_name('seh_cookie_fixture.py'),overlay),
        strict_modes=(),guest_programs=programs,codegen_setup=None if a.without_overlay else install,
        translator_options={'seh_prolog':0x401900,'seh_epilog':0x401960})

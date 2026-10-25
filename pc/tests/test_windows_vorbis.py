"""Compare translated callback decoding with native Xiph; no audio device."""
import argparse
import json
from pathlib import Path
import struct
import sys
import test_windows_heap as shared

def programs(raw):
    result={0x401cc0:bytes.fromhex('31c0c21000')}
    for index,count in enumerate((4,4,1,1)):
        iat=struct.unpack_from('<I',raw,0x300+(6+index)*20+16)[0]+0x400000
        result[0x401d00+index*64]=bytes([0xff,0x74,0x24,count*4])*count+b'\xff\x15'+struct.pack('<I',iat)+b'\xc3'
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output','vorbis-build'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);p.add_argument('--x87-build',type=Path);a=p.parse_args();build=json.loads(a.vorbis_build.read_text())
    if build['status']!='COMPILED' or build['switch_abi']:raise ValueError('Host codec build required')
    for path,h in build['source_sha256'].items():
        if shared.sha(Path(path))!=h:raise ValueError('Codec source changed')
    library=Path(build['library']);assert shared.sha(library)==build['library_sha256'];a.cc.extend('-I'+x for x in build['includes'])
    shared.NAMES=['ov_open_callbacks','ov_clear','ov_pcm_total','ov_seekable','ov_read','ov_info','TestRead','TestSeek','TestClose','TestTell'];shared.ARGS=[8,1,2,1,7,2,4,4,1,1];shared.DLLS=['libvorbisfile.dll']*6+['vorbis-test.dll']*4
    pc=Path(__file__).resolve().parents[1]
    setup=None;extra=[Path(__file__).resolve(),pc/'native/windows_vorbis.h',a.vorbis_build.resolve(),library];link=['-Wl,--stack,8388608',str(library),'-lm']
    if a.x87_build:
        sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve()),str(pc/'tools')]
        from test_x87_environment import verify
        from aot_x87_extended import install
        core=json.loads(a.x87_build.read_text());verify(core)
        if core['switch_abi']:raise ValueError('Host x87 core required')
        objects=[Path(row['object']) for row in core['objects']];extra.extend([a.x87_build.resolve(),*objects,*[Path(n) for n in core['source_sha256']],pc/'tools/aot_x87_extended.py']);link.extend(str(o) for o in objects)
        a.cc.append('-DFNV_CPU_EXTENDED_X87=1');setup=install
    shared.test(a,host_source='windows_vorbis_host.c',wrapper_stride=64,guest_programs=programs,native_sources=('windows_vorbis.c',),
        extra_inputs=extra,codegen_setup=setup,
        linker_options=link,cdecl_imports=tuple(range(6)),
        strict_modes=('--strict-callback-abi','--strict-output','--strict-duplicate'))

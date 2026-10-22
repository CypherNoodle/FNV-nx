"""Real managed host threads execute SDK-translated x86 ThreadProcs and imports."""
import argparse
from pathlib import Path
import struct
import test_windows_heap as shared

def programs(raw):
    def call(name):
        i=shared.NAMES.index(name);iat=struct.unpack_from('<5I',raw,0x300+i*20)[4]
        return b'\xff\x15'+struct.pack('<I',0x400000+iat)
    # EBX is a guest pointer; each thread has a separate results structure.
    prefix=b'\x53\x8b\x5c\x24\x08'
    body=(call('GetCurrentThreadId')+b'\x89\x03'+b'\x64\xa1\x18\x00\x00\x00\x89\x43\x04'
          +b'\xff\x73\x08'+call('Sleep')
          +b'\x68\x78\x56\x34\x12\xff\x73\x0c'+call('TlsSetValue')
          +b'\xff\x73\x0c'+call('TlsGetValue')+b'\x89\x43\x20')
    ret=b'\x8b\x43\x14\x5b\xc2\x04\x00'
    explicit=(prefix+body+b'\xff\x73\x14'+call('ExitThread')
              +b'\xc7\x43\x18\xef\xbe\xad\xde'+ret)
    mutex=(prefix+b'\x6a\x00\xff\x73\x1c'+call('WaitForSingleObject'))
    mutex+=b'\x6a\x00\xff\x73\x1c'+call('WaitForSingleObject')+ret
    nested=(prefix+b'\x6a\x00\x6a\x00\x8d\x43\x40\x50\x68\x00\x1c\x40\x00\x6a\x00\x6a\x00'
            +call('CreateThread')+b'\x89\x43\x24\x6a\xff\x50'+call('WaitForSingleObject')
            +b'\xff\x73\x24'+call('CloseHandle')+ret)
    result={0x401c00:prefix+body+ret,0x401c80:explicit,0x401d00:mutex,0x401d80:nested,
            0x401df0:b'\xb8\x01\x00\x00\x00\xc3'} # deliberately wrong ThreadProc cleanup
    assert all(len(code)<128 for code in result.values())
    return result

def test_workers(a,extra_inputs=()):
    shared.NAMES=['CreateThread','ExitThread','GetExitCodeThread','WaitForSingleObject','CloseHandle',
                  'GetCurrentThreadId','GetThreadId','GetCurrentThread','ResumeThread','Sleep',
                  'SetLastError','GetLastError','TlsSetValue','TlsGetValue','CreateMutexA',
                  'WaitForMultipleObjects','TlsAlloc']
    shared.ARGS=[6,1,2,2,1,0,1,0,1,1,1,0,2,1,3,4,0]
    shared.test(a,host_source='windows_workers_host.c',extra_inputs=(Path(__file__).resolve(),*extra_inputs),
                linker_options=('-Wl,--stack,16777216',),
                wrapper_stride=64,guest_programs=programs,
                strict_modes=('--strict-flags','--strict-disabled','--strict-peer','--strict-start',
                              '--strict-bad-stack','--strict-peer-suspended','--strict-exit','--strict-static-tls'))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    test_workers(p.parse_args())

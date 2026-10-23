"""Translated Bink sound-selection callbacks, state, ABI and explicit stops."""
import argparse
import struct
import json
import hashlib
import subprocess
from pathlib import Path
import test_windows_heap as shared


def programs(raw):
    raw.extend(bytes(12288-len(raw)))
    struct.pack_into('<I',raw,0x98+56,0x4000)
    struct.pack_into('<I',raw,0x98+224+8,len(raw)-512)
    struct.pack_into('<I',raw,0x98+224+16,len(raw)-512)
    iat=0x400000+struct.unpack_from('<I',raw,0x300+16)[0]
    return {
        0x401c00:bytes.fromhex('8b442404a300004800ff0504004800a108004800c20400'),
        0x401c40:b'\x6a\x00\x68'+struct.pack('<I',0x401c00)+b'\xff\x15'+struct.pack('<I',iat)+b'\xc2\x04\x00',
        0x401c80:bytes.fromhex('31c0c3'),
        0x401d00:bytes.fromhex('31c0c21800'),
        0x401d20:bytes.fromhex('31c0c21800')}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    p.add_argument('--original-bink',type=Path,help='Optional original x86 DLL, read-only reference')
    a=p.parse_args()
    shared.NAMES=['_BinkSetSoundSystem@8','_BinkOpenDirectSound@4','GetLastError','SetLastError']
    shared.ARGS=[2,1,0,1];shared.DLLS=['binkw32.dll','binkw32.dll','kernel32.dll','kernel32.dll']
    shared.test(a,host_source='bink_sound_host.c',guest_programs=programs,native_sources=('bink_sound.c',),
        extra_inputs=(Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'native/bink_sound.h'),
        linker_options=('-Wl,--stack,8388608',),
        strict_modes=('--strict-callback','--strict-stack','--strict-backend','--strict-recursion','--strict-abi'))
    if a.original_bink:
        dll=a.original_bink.resolve();oracle=Path(__file__).with_name('bink_sound_x86_oracle.ps1')
        sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();frozen={str(p):sha(p) for p in (dll,oracle)}
        run=subprocess.run(['C:/Windows/SysWOW64/WindowsPowerShell/v1.0/powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(oracle),'-DllPath',str(dll)],capture_output=True,text=True,timeout=30)
        (a.output/'native-oracle.log').write_text(run.stdout+run.stderr);assert not run.returncode,run.stderr
        native=json.loads(run.stdout.strip().splitlines()[-1]);assert native['status']=='PASS' and native['native_sound_selection_calls']==2004
        assert all(sha(Path(p))==h for p,h in frozen.items())
        proof=json.loads((a.output/'validation.json').read_text());proof.update(native_oracle=native,native_oracle_sha256=frozen)
        (a.output/'validation.json').write_text(json.dumps(proof,indent=2)+'\n')

"""Translated custom MMIO callbacks, buffered reads and RIFF against Windows."""
import argparse
import json
import os
import subprocess
from pathlib import Path
import struct
import test_windows_heap as shared

def programs(raw):
    iat=struct.unpack_from('<I',raw,0x300+9*20+16)[0]
    return {0x401d00:bytes((0xff,0x74,0x24,16))*4+b'\xff\x15'+struct.pack('<I',0x400000+iat)+b'\xc2\x10\x00'}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    shared.NAMES=['mmioOpenA','mmioClose','mmioRead','mmioSeek','mmioGetInfo','mmioSetInfo','mmioAdvance','mmioDescend','mmioAscend','TestIO']
    shared.ARGS=[3,2,3,3,3,3,3,4,3,4];shared.DLLS=['winmm.dll']*9+['test-mmio.dll']
    pc=Path(__file__).resolve().parents[1]
    a=p.parse_args();oracle=pc/'tests/windows_mmio_error_x86_oracle.ps1'
    shared.test(a,host_source='windows_mmio_host.c',guest_programs=programs,
        native_sources=('windows_mmio.c',),extra_inputs=(Path(__file__).resolve(),pc/'native/windows_mmio.h',oracle),
        linker_options=('-Wl,--stack,16777216','-lwinmm'),
        strict_modes=('--strict-open-mode','--strict-output','--strict-callback-abi','--strict-set-info','--strict-end-offset'))
    powershell=Path(os.environ['WINDIR'])/'SysWOW64/WindowsPowerShell/v1.0/powershell.exe'
    child=subprocess.run([str(powershell),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(oracle)],capture_output=True,text=True,timeout=30)
    (a.output/'x86-error-oracle.log').write_text(child.stdout+child.stderr)
    if child.returncode:raise RuntimeError(child.stdout+child.stderr)
    result=json.loads(child.stdout.strip());assert result['status']=='PASS'
    validation=a.output/'validation.json';proof=json.loads(validation.read_text());proof['x86_error_field_oracle']=result
    validation.write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps(result))

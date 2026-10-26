"""Translated yaw/pitch/roll calls checked against x64 and x86 Windows D3DX."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import test_windows_heap as shared


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);a=p.parse_args();pc=Path(__file__).resolve().parents[1]
    shared.NAMES=['D3DXMatrixRotationYawPitchRoll','GetLastError','SetLastError'];shared.ARGS=[4,0,1]
    shared.DLLS=['d3dx9_38.dll','kernel32.dll','kernel32.dll']
    oracle=pc/'tests/d3dx_rotation_x86_oracle.ps1'
    shared.test(a,host_source='d3dx_rotation_host.c',native_sources=('windows_d3dx.c',),
        extra_inputs=(Path(__file__).resolve(),pc/'tests/windows_d3dx_host.c',pc/'native/windows_d3dx.h',pc/'native/dds_image_info.h',oracle,
                      Path('C:/Windows/System32/d3dx9_38.dll'),Path('C:/Windows/SysWOW64/d3dx9_38.dll')),
        strict_modes=('--strict-null','--strict-range','--strict-nan','--strict-denormal','--strict-angle','--strict-rounding'),
        linker_options=('-Wl,--stack,8388608',))
    src=a.output.resolve()/'x86-input.bin';dst=a.output.resolve()/'x86-oracle.bin';seed=0x76172
    with src.open('xb') as stream:
        stream.write(struct.pack('<I',5000))
        for i in range(5000):
            angles=[]
            for j in range(3):seed=(seed*1664525+1013904223)&0xffffffff;angles.append(((seed>>8)-0x800000)/262144)
            if i<4:
                angles=[0.,0.,0.]
                if i:angles[i-1]=1.57079632679
            if i==4:angles=[64.,-64.,64.]
            stream.write(struct.pack('<3f',*angles))
    command=['C:/Windows/SysWOW64/WindowsPowerShell/v1.0/powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(oracle),'-InputPath',str(src),'-OutputPath',str(dst)]
    r=subprocess.run(command,capture_output=True,text=True,timeout=60);(a.output/'x86-oracle.log').write_text(r.stdout+r.stderr);r.check_returncode()
    r=subprocess.run([str(a.output.resolve()/'heap-test.exe'),str(a.output.resolve()/'fixture.exe'),'--oracle',str(dst)],capture_output=True,text=True,timeout=45,env=shared.compiler_environment(a.cc))
    (a.output/'x86-host.log').write_text(r.stdout+r.stderr);r.check_returncode()
    result=json.loads(r.stdout);report=json.loads((a.output/'validation.json').read_text());report.update(x86_rotation_matrices=result['rotation_matrices'],x86_max_absolute_error=result['max_absolute_error'])
    for source,digest in report['source_sha256'].items():assert shared.sha(Path(source))==digest
    (a.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(result))

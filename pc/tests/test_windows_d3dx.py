"""Translated D3DX matrix calls compared bitwise with the installed Windows DLL."""
import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path
import test_windows_heap as shared
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);a=p.parse_args()
    shared.NAMES=['D3DXMatrixMultiply','GetLastError','SetLastError','D3DPERF_SetOptions'];shared.ARGS=[3,0,1,1];shared.DLLS=['d3dx9_38.dll','kernel32.dll','kernel32.dll','d3d9.dll']
    pc=Path(__file__).resolve().parents[1];oracle=pc/'tests/d3dx_matrix_x86_oracle.ps1';dll32=Path('C:/Windows/SysWOW64/d3dx9_38.dll')
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();frozen={str(p):sha(p) for p in (oracle,dll32)}
    shared.test(a,host_source='windows_d3dx_host.c',native_sources=('windows_d3dx.c',),extra_inputs=(Path(__file__).resolve(),pc/'native/windows_d3dx.h',pc/'native/dds_image_info.h',Path('C:/Windows/System32/d3dx9_38.dll'),oracle,dll32),linker_options=('-Wl,--stack,8388608',),strict_modes=('--strict-null','--strict-range','--strict-nan','--strict-denormal','--strict-overflow','--strict-rounding','--strict-perf'))
    source=a.output.resolve()/'x86-input.bin';output=a.output.resolve()/'x86-oracle.bin';seed=0x71acdef
    with source.open('xb') as stream:
        stream.write(struct.pack('<I',10000))
        for case in range(10000):
            matrices=[[],[]]
            for i in range(16):
                for matrix in matrices:
                    seed=(seed*1664525+1013904223)&0xffffffff;matrix.append(((seed>>8)-0x800000)/1024)
            stream.write(struct.pack('<32f',*matrices[0],*matrices[1]))
    command=['C:/Windows/SysWOW64/WindowsPowerShell/v1.0/powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(oracle),'-InputPath',str(source),'-OutputPath',str(output)]
    result=subprocess.run(command,capture_output=True,text=True,timeout=60);(a.output/'x86-oracle.log').write_text(result.stdout+result.stderr);assert not result.returncode,result.stderr
    result=subprocess.run([str(a.output.resolve()/'heap-test.exe'),str(a.output.resolve()/'fixture.exe'),'--x86-oracle',str(output)],capture_output=True,text=True,timeout=45)
    (a.output/'x86-host.log').write_text(result.stdout+result.stderr);assert not result.returncode,result.stderr
    assert all(sha(Path(p))==h for p,h in frozen.items())
    report=json.loads((a.output/'validation.json').read_text());report.update(x86_windows_oracle=True,x86_matrix_comparisons=10000,x86_oracle_sha256=sha(output))
    (a.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS x86 Windows D3DX 10000 matrices, four aliases and seven strict stops')

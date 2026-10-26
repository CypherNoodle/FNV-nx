"""Translate eight D3DX math APIs and compare with x86/x64 Windows DLLs."""
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
    names=['D3DXMatrixTranspose','D3DXMatrixInverse','D3DXVec4Transform','D3DXVec3TransformCoord',
           'D3DXVec3TransformNormal','D3DXPlaneTransform','D3DXVec3Normalize','D3DXPlaneNormalize']
    shared.NAMES=names+['GetLastError','SetLastError'];shared.ARGS=[2,3,3,3,3,3,2,2,0,1]
    shared.DLLS=['d3dx9_38.dll']*8+['kernel32.dll']*2
    oracle=pc/'tests/d3dx_math_x86_oracle.ps1'
    shared.test(a,host_source='d3dx_math_host.c',native_sources=('windows_d3dx.c',),
        extra_inputs=(Path(__file__).resolve(),pc/'tests/windows_d3dx_host.c',pc/'native/windows_d3dx.h',pc/'native/dds_image_info.h',pc/'native/d3dx_vector_matrix.inc',oracle,
                      Path('C:/Windows/System32/d3dx9_38.dll'),Path('C:/Windows/SysWOW64/d3dx9_38.dll')),
        strict_modes=('--strict-nan','--strict-denormal','--strict-range','--strict-null','--strict-overflow',
                      '--strict-plane','--strict-coord','--strict-inverse','--strict-determinant-alias','--strict-rounding'),
        linker_options=('-Wl,--stack,8388608',))
    src=a.output.resolve()/'x86-input.bin';dst=a.output.resolve()/'x86-oracle.bin';seed=0x7162a
    with src.open('xb') as f:
        f.write(struct.pack('<I',1000))
        for case in range(1000):
            m=[];v=[]
            for i in range(16):seed=(seed*1664525+1013904223)&0xffffffff;m.append(((seed>>8)-0x800000)/33554432)
            for i in range(4):seed=(seed*1664525+1013904223)&0xffffffff;m[i*5]+=4;v.append(((seed>>8)-0x800000)/4194304)
            if case==0:v=[0.]*4
            if case==1:m=[1.5,0,0,0,0,2,0,0,0,0,1000/999,1,0,0,-1000/999,0];v[2]=2
            if case==2:m=[2,0,0,0,0,-3,0,0,0,0,4,0,10000,20000,-30000,1]
            f.write(struct.pack('<20f',*m,*v))
    cmd=['C:/Windows/SysWOW64/WindowsPowerShell/v1.0/powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(oracle),'-InputPath',str(src),'-OutputPath',str(dst)]
    r=subprocess.run(cmd,capture_output=True,text=True,timeout=60);(a.output/'x86-oracle.log').write_text(r.stdout+r.stderr);r.check_returncode()
    r=subprocess.run([str(a.output.resolve()/'heap-test.exe'),str(a.output.resolve()/'fixture.exe'),'--oracle',str(dst)],capture_output=True,text=True,env=shared.compiler_environment(a.cc),timeout=60)
    (a.output/'x86-host.log').write_text(r.stdout+r.stderr);r.check_returncode();result=json.loads(r.stdout)
    report=json.loads((a.output/'validation.json').read_text());report.update(x86_windows=result)
    for source,digest in report['source_sha256'].items():assert shared.sha(Path(source))==digest
    (a.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(result))

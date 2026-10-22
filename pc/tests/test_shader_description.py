"""Own shader reflection, duplicate records, centroid accounting and rejection."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
from test_d3d9_spirv import VS,PS,compile_hlsl
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from describe_d3d9_package import describe

def run(a):
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    dll=C.WinDLL(str(Path(os.environ['SystemRoot'])/'System32/d3dcompiler_47.dll'))
    v=compile_hlsl(dll,VS,b'vs_3_0');p=compile_hlsl(dll,PS.replace(b'float2 uv:',b'centroid float2 uv:'),b'ps_3_0')
    unsupported=struct.pack('<III',0xfffe0300,0xdead,0xffff)
    payload=b''
    for i,blob in enumerate((v,p,v,unsupported)):
        payload+=(f'own{i}'.encode()+b'\0').ljust(256,b'\0')+struct.pack('<I',len(blob))+blob
    data=struct.pack('<III',100,4,len(payload))+payload;package=out/'own.sdp';package.write_bytes(data)
    digest=lambda b:hashlib.sha256(b).hexdigest()
    result=describe(package,digest(data),a.linker.resolve(),out/'description')
    assert len(result['records'])==4 and len(result['unique'])==3 and result['accepted']==2 and result['rejected']==1
    vs=result['unique'][digest(v)];ps=result['unique'][digest(p)]
    assert vs['metadata']['stage']==2 and ps['metadata']['stage']==1
    assert vs['metadata']['inputs']==[dict(usage=0,index=0),dict(usage=5,index=0)]
    assert ps['metadata']['inputs']==[dict(usage=5,index=0)]
    assert vs['metadata']['samplers']==[] and ps['metadata']['samplers']==[dict(index=0,type=0,texbem=False)]
    assert vs['metadata']['uniforms']==ps['metadata']['uniforms']==[dict(kind=0,first=0,count=1,local=False)]
    assert vs['centroid_inputs']==0 and ps['centroid_inputs']==1
    assert not result['unique'][digest(unsupported)]['accepted']
    assert not list((out/'description').glob('sdp-*')),'temporary bytecode not removed'
    try:describe(package,'0'*64,a.linker.resolve(),out/'wrong-identity')
    except ValueError:pass
    else:raise AssertionError('wrong identity accepted')
    assert not (out/'wrong-identity').exists() and package.read_bytes()==data
    report=dict(status='PASS',synthetic_records=4,unique_shaders=3,rejected_bytecode=True,wrong_identity_rejected=True,centroid_accounted=True,game_data_used=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--linker',type=Path,required=True);run(p.parse_args())

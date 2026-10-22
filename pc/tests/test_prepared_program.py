"""Own shader -> linked pair -> private header, with metadata mismatch rejection."""
import argparse
import ctypes as C
import copy
import json
import os
from pathlib import Path
import sys
from types import SimpleNamespace
from test_d3d9_spirv import VS,PS,compile_hlsl
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from emit_prepared_program import emit
from translate_d3d9_pair import translate

def run(a):
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    dll=C.WinDLL(str(Path(os.environ['SystemRoot'])/'System32/d3dcompiler_47.dll'))
    for s,source in (('vs',VS),('ps',PS)):(out/(s+'.bin')).write_bytes(compile_hlsl(dll,source,(s+'_3_0').encode()))
    (out/'attributes.txt').write_text('0 0 1\n5 0 1\n')
    report=translate(SimpleNamespace(vs=out/'vs.bin',ps=out/'ps.bin',attributes=out/'attributes.txt',linker=a.linker.resolve(),output=out/'pair'))
    locations={v['name']:v['location'] for v in report['stages']['vs']['spirv']['interfaces'] if v['storage']==1 and 'location' in v}
    inputs=[[0,0,locations['vs_v0']],[5,0,locations['vs_v1']]]
    proof=emit(out,inputs,1,out/'program.h');assert proof['status']=='PASS'
    manifest=out/'pair/translation.json';negative=0
    def reject(name,mapping=inputs,mask=1):
        nonlocal negative
        dest=out/(name+'.h')
        try:emit(out,mapping,mask,dest)
        except ValueError:negative+=1
        else:raise AssertionError('invalid metadata accepted: '+name)
        assert not dest.exists()
    reject('wrong-mask',mask=2);reject('unknown-location',mapping=[[0,0,7],[5,0,8]])
    reject('duplicate-location',mapping=[[0,0,0],[5,0,0]])
    wrong=copy.deepcopy(report);wrong['stages']['vs']['uniform_plan']['bytes']+=16
    manifest.write_text(json.dumps(wrong));reject('wrong-plan');manifest.write_text(json.dumps(report))
    for filename in ('vs.bin','pair/ps.spv'):
        path=out/filename;original=path.read_bytes();path.write_bytes(original[:-4]+b'\0\0\0\0');reject('wrong-'+path.suffix[1:]);path.write_bytes(original)
    assert negative==6
    try:emit(out,inputs,1,out/'program.h')
    except FileExistsError:pass
    else:raise AssertionError('header overwritten')
    result=dict(status='PASS',negative_cases=negative,overwrite_rejected=True,private_header=True,game_data_used=False,header_sha256=proof['header_sha256'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--linker',type=Path,required=True);run(p.parse_args())

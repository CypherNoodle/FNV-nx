"""Reject stale driver/ABI/objects when reusing a bounded CRT diagnostic."""
import argparse
import copy
import json
from pathlib import Path
import sys


def test(out):
    if out.exists():raise ValueError('Fresh output required')
    out=out.resolve();out.mkdir(parents=True);sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
    from startup_object_reuse import reusable
    from run_aot_analysis import sha
    old=out/'old';new=out/'new';native=out/'native'
    for d in (old,new,native):d.mkdir()
    compiler=out/'compiler.bin';compiler.write_bytes(b'driver');cc=[str(compiler),'cc'];flags=['-O1','-DFNV_CPU_EXTENDED_X87=1']
    h=native/'abi.h';h.write_text('#define ABI 1\n');c=native/'runtime.c';c.write_text('void native(void){}\n')
    core=out/'core.json';core.write_text(json.dumps(dict(compiler=cc)));digest=sha(core)
    generated={}
    for name,text in [('generated_0000.c','void guest(void){}\n'),('generated_functions.h','void guest(void);\n'),('startup_profile.h','#define ENTRY 1\n')]:
        for d in (old,new):(d/name).write_text(text)
        generated[name]=sha(new/name)
    for name in ('generated_0000.o','runtime.o'):(old/name).write_bytes(name.encode())
    prior=dict(status='EXPERIMENT_STOPPED',all_generated=True,compiler_flags=flags,
        host_x87=dict(schema='fnv-startup-host-x87-v1',core_validation_sha256=digest),
        source_sha256={str(p):sha(p) for p in (core,compiler,h,c)},generated_files_sha256=generated.copy(),
        compiled=[dict(source='generated_0000.c',object='generated_0000.o',object_sha256=sha(old/'generated_0000.o')),
                  dict(source='runtime.c',object='runtime.o',object_sha256=sha(old/'runtime.o'))])
    pp=old/'validation.json';sources=[new/'generated_0000.c',c];expected={s.name for s in sources};checks=0
    def call(info=None,current_flags=None,current_cc=None):
        nonlocal checks
        pp.write_text(json.dumps(prior if info is None else info));frozen={str(p):sha(p) for p in (h,c)}
        result=reusable(pp,cc if current_cc is None else current_cc,flags if current_flags is None else current_flags,
                        sources,generated,new,frozen,[native]);checks+=1;return result,frozen
    def reject(**kw):
        nonlocal checks
        try:call(**kw)
        except ValueError:checks+=1
        else:raise AssertionError('Stale diagnostic cache accepted')
    result,frozen=call();assert set(result)==expected and str(old/'generated_0000.o') in frozen
    reject(current_flags=['-O2']);reject(current_cc=[str(compiler),'clang'])
    for key,value in [('status','CANDIDATE_ONLY_NOT_PACKAGING_EVIDENCE'),('all_generated',False),('compiled',[])]:
        bad=copy.deepcopy(prior);bad[key]=value;reject(info=bad)
    compiler.write_bytes(b'changed');reject();compiler.write_bytes(b'driver')
    h.write_text('#define ABI 2\n');reject();h.write_text('#define ABI 1\n')
    p=new/'generated_functions.h';p.write_text('void changed(void);');reject();p.write_text('void guest(void);\n')
    p=old/'startup_profile.h';p.write_text('#define ENTRY 2\n');reject();p.write_text('#define ENTRY 1\n')
    p=old/'generated_0000.c';p.write_text('void changed(void){}');reject();p.write_text('void guest(void){}\n')
    p=old/'generated_0000.o';p.write_bytes(b'changed');reject();p.write_bytes(b'generated_0000.o')
    p=new/'generated_0000.c';p.write_text('void changed(void){}');assert set(call()[0])=={'runtime.c'};p.write_text('void guest(void){}\n')
    c.write_text('void native(void){(void)0;}');assert set(call()[0])=={'generated_0000.c'};c.write_text('void native(void){}\n')
    bad=copy.deepcopy(prior);bad['compiled'].append(bad['compiled'][0]);reject(info=bad)
    bad=copy.deepcopy(prior);bad['compiled'][0]['object']='../escape.o';reject(info=bad)
    assert set(call()[0])==expected
    paths=[Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'tools/startup_object_reuse.py']
    result=dict(status='PASS',checks=checks,objects_executed=False,game_code_used=False,
                source_sha256={str(p):sha(p) for p in paths})
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS',checks,'diagnostic object identity checks')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);test(p.parse_args().output)

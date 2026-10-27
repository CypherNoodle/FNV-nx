"""Real ARM64 compiler contract checks using three own synthetic C units."""
import argparse
import copy
import json
from pathlib import Path
import re
from types import SimpleNamespace
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build_switch_engine_generation import build,ABI_HEADERS
from run_aot_analysis import sha


def run(a):
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    pc=Path(__file__).resolve().parents[1];gen=out/'own-generation';gen.mkdir()
    sources={
        'recomp_0000.c':'#include "recomp_funcs.h"\nvoid own_leaf(void){g_eax^=0x13579bdfu;}\n',
        'recomp_dispatch.c':'#include "recomp_funcs.h"\ntypedef struct {uint32_t va;recomp_func_t fn;} recomp_entry_t;\nstatic const recomp_entry_t g_recomp_table[]={\n {0x00001000u,(recomp_func_t)own_leaf},\n};\nrecomp_func_t recomp_lookup(uint32_t va){return va==g_recomp_table[0].va?g_recomp_table[0].fn:0;}\n',
        'recomp_stubs_unresolved.c':'void own_unused_stub(void){}\n',
        'recomp_funcs.h':'#include "recomp_types.h"\nvoid own_leaf(void);\n'}
    for name,text in sources.items():(gen/name).write_text(text)
    cpu=json.loads(a.cpu_validation.read_text());runtime=json.loads(a.runtime_validation.read_text())
    cpu['compiled']=[dict(source=name,exit_code=0) for name in sources if name.endswith('.c')]
    cpu['compile_units']=3;cpu['synthetic_test_only']=True
    for name in sources:cpu['source_sha256'][str(gen/name)]=sha(gen/name)
    proof=out/'synthetic-cpu-proof.json';proof.write_text(json.dumps(cpu,indent=2)+'\n')
    args=SimpleNamespace(output=out/'compiled',generation=gen,cpu_validation=proof,
                         runtime_validation=a.runtime_validation.resolve(),sdk=a.sdk.resolve(),
                         cc=a.cc.resolve(),libnx=a.libnx.resolve(),jobs=1)
    result=build(args)
    assert result['status']=='COMPILED' and len(result['compiled'])==3 and not result['game_started']
    for row in result['compiled']:
        r=subprocess.run([str(a.readelf.resolve()),'-h',row['object']],capture_output=True,text=True,check=True)
        assert re.search(r'Machine:\s+AArch64\b',r.stdout)
    cases=[]
    for name in ('incomplete-inventory','changed-source','wrong-abi'):
        bad=copy.deepcopy(cpu)
        if name=='incomplete-inventory':bad['compiled']=bad['compiled'][:-1];expected='unit inventory differs'
        elif name=='changed-source':bad['source_sha256'][str(gen/'recomp_0000.c')]='0'*64;expected='Changed generation ABI input'
        else:bad['compiler_flags'].append('-fshort-enums');expected='ABI disagree'
        path=out/(name+'.json');path.write_text(json.dumps(bad)+'\n')
        negative=copy.copy(args);negative.cpu_validation=path;negative.output=out/name
        try:build(negative)
        except ValueError as error:
            assert expected in str(error),str(error)
        else:raise AssertionError(name+' unexpectedly compiled')
        assert not negative.output.exists();cases.append(name)
    report=dict(status='PASS',own_synthetic_units=3,aarch64_headers_checked=3,
                rejected_before_compile=cases,game_started=False,switch_hardware_tested=False,
                source_sha256={str(Path(__file__).resolve()):sha(Path(__file__).resolve()),
                               str(a.cpu_validation.resolve()):sha(a.cpu_validation.resolve()),
                               str(a.runtime_validation.resolve()):sha(a.runtime_validation.resolve())})
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('cpu-validation','runtime-validation','sdk','cc','readelf','libnx','output'):
        p.add_argument('--'+name,type=Path,required=True)
    run(p.parse_args())

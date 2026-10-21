"""Exercise private-generation proof rejection without changing any build input."""
import argparse
from copy import deepcopy
import json
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from generated_subset import read_generation
from run_aot_analysis import sdk_identity,sha


def test(a):
    if a.output.exists():raise ValueError('Use a fresh private directory')
    sys.path.insert(0,str(a.python_deps.resolve()))
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True
    source=json.loads(a.validation.read_text());sdk_hash=sdk_identity(a.sdk.resolve())
    a.output.mkdir(parents=True);checks=0
    functions,references,symbols,inputs,info=read_generation(
        a.analysis.resolve(),a.generation_name,a.validation.resolve(),decoder,sdk_hash)
    assert functions and set(functions)==set(references)==set(symbols)
    assert info['functions_available']==len(functions) and inputs
    positive_functions=len(functions)
    variants=[('report-hash','variant_report_sha256','0'*64),
              ('body-change','retained_function_bodies_identical',False),
              ('selection','selection',None),('status','status','GENERATED_WITH_GAPS'),
              ('unfrozen','source_sha256',None)]
    for label,key,value in variants:
        changed=deepcopy(source)
        if value is None:changed.pop(key,None)
        else:changed[key]=value
        path=a.output/(label+'.json');path.write_text(json.dumps(changed))
        try:read_generation(a.analysis.resolve(),a.generation_name,path,decoder,sdk_hash)
        except ValueError:checks+=1
        else:raise AssertionError('Accepted '+label)
    try:read_generation(a.analysis.resolve(),a.generation_name,a.validation.resolve(),decoder,'0'*64)
    except ValueError:checks+=1
    else:raise AssertionError('Accepted changed SDK')
    changed=deepcopy(source);key=next(iter(changed['source_sha256']));changed['source_sha256'][key]='0'*64
    path=a.output/'changed-input.json';path.write_text(json.dumps(changed))
    try:read_generation(a.analysis.resolve(),a.generation_name,path,decoder,sdk_hash)
    except ValueError:checks+=1
    else:raise AssertionError('Accepted changed dependency')
    pc=Path(__file__).resolve().parents[1]
    paths=[Path(__file__).resolve(),pc/'tools/generated_subset.py',pc/'tools/build_cpu_runtime.py',
           pc/'tools/verify_aot_code_data.py',pc/'tools/aot_code_data.py',
           pc/'tools/aot_table_selection.py',pc/'tools/audit_orphan_jump_tables.py',a.validation.resolve()]
    result=dict(status='PASS',negative_checks=checks,accepted_functions=positive_functions,generated_sources_modified=False,
                generated_code_executed=False,sdk_sha256=sdk_hash,source_sha256={str(p):sha(p) for p in paths})
    (a.output/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS',checks,'incompatible generation proofs rejected')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('analysis','validation','sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--generation-name',required=True);test(p.parse_args())

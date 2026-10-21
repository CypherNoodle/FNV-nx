"""Reject incompatible code-generation proofs before any compiler runs."""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from verify_aot_code_data import matching_generation_inputs
from build_cpu_runtime import validate_selection_proof


def run(output):
    if output.exists():raise ValueError('Use a fresh private report')
    checks=0
    def reject(call):
        nonlocal checks
        try:call()
        except ValueError:checks+=1
        else:raise AssertionError('Invalid proof accepted')
    fields=('input_profile_sha256','sdk_sha256','index_wrapper_sha256',
            'codegen_overlay','multiply_overlay','frame_restore_overlay','normal_frame_helpers','eh3_frame_overlay')
    report={key:{'source_sha256':'a'} if key.endswith('overlay') else 'a' for key in fields}
    report['normal_frame_helpers']={'abi':'fixture'}
    matching_generation_inputs(report,deepcopy(report));checks+=1
    for key in fields:
        changed=deepcopy(report);changed[key]={'unexpected':'revision'}
        reject(lambda:matching_generation_inputs(report,changed))
    for key in fields[:5]:
        changed=deepcopy(report);del changed[key]
        reject(lambda:matching_generation_inputs(changed,changed))
    incompatible=deepcopy(report);incompatible['frame_restore_overlay']=None
    reject(lambda:matching_generation_inputs(incompatible,incompatible))
    legacy={k:v for k,v in report.items() if k not in ('normal_frame_helpers','frame_restore_overlay')}
    matching_generation_inputs(legacy,deepcopy(legacy));checks+=1
    extended=deepcopy(report)
    runner=str(Path(__file__).resolve().parents[1]/'tools/run_aot_analysis.py')
    extended['x87_extended_overlay']={'profile':'fixture','source_sha256':{runner:'old','math.c':'stable'}}
    routed=deepcopy(extended);routed['x87_extended_overlay']['source_sha256'][runner]='new'
    matching_generation_inputs(extended,routed);checks+=1
    for field in ('profile','source_sha256'):
        bad=deepcopy(routed)
        if field=='profile':bad['x87_extended_overlay'][field]='different'
        else:bad['x87_extended_overlay'][field]['math.c']='changed'
        reject(lambda:matching_generation_inputs(extended,bad))
    reject(lambda:matching_generation_inputs(report,routed))
    selection={'excluded_entries':[16]};generation={'code_data_filter':selection}
    audit={'status':'PASS','retained_function_bodies_identical':True,
           'variant_report_sha256':'report-hash','selection':selection}
    assert validate_selection_proof(audit,selection,generation,'report-hash') is False;checks+=1
    structure={**audit,'status':'STRUCTURE_PASS','audit_kind':'table-selection-structure-v1',
               'objects_compiled':False,'compiled':[],'source_sha256':{'fixture':'digest'}}
    assert validate_selection_proof(structure,selection,generation,'report-hash') is True;checks+=1
    for key,value in [('status','GENERATED_WITH_GAPS'),('audit_kind','unrecognized'),
                      ('objects_compiled',True),('objects_compiled',0),('compiled',[{}]),
                      ('compiled',None),('source_sha256',{}),('retained_function_bodies_identical',False),
                      ('retained_function_bodies_identical',1),('variant_report_sha256','other'),
                      ('selection',{'excluded_entries':[]})]:
        changed=deepcopy(structure);changed[key]=value
        reject(lambda:validate_selection_proof(changed,selection,generation,'report-hash'))
    reject(lambda:validate_selection_proof(structure,selection,{'code_data_filter':None},'report-hash'))
    for key in structure:
        changed=deepcopy(structure);del changed[key]
        reject(lambda:validate_selection_proof(changed,selection,generation,'report-hash'))
    paths=[Path(__file__).resolve(),*[Path(__file__).resolve().parents[1]/'tools'/n for n in
           ('verify_aot_code_data.py','build_cpu_runtime.py')]]
    result={'status':'PASS','checks':checks,'compiler_executed':False,'native_engine_started':False,
            'source_sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}}
    output.parent.mkdir(parents=True,exist_ok=True);output.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS',checks,'generation-proof acceptance/rejection checks; no compiler run')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    run(p.parse_args().output)

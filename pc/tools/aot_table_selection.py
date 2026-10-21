"""Compose a verified indexed-table selection over a preserved code/data parent.

Only the extra orphan seed entries are removed. Parent code, table-tail trims,
and every retained function record remain byte-for-byte unchanged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys

from aot_code_data import (address,filtered_files,load_inputs,
                          verify_derivation as verify_parent)
from audit_aot_gaps import sections_from_container
from audit_orphan_jump_tables import find_tables

SCHEMA='fnv-indexed-table-selection-v1'

def digest(data):return hashlib.sha256(data).hexdigest()

def source_hashes():
    directory=Path(__file__).resolve().parent
    return {name:digest((directory/name).read_bytes()) for name in (
        'aot_table_selection.py','audit_orphan_jump_tables.py','aot_code_data.py','audit_aot_gaps.py')}

def findings(parent,decoder):
    raw,profile,scan,files,identity=load_inputs(parent)
    if 'indexed_table_filter' in scan:raise ValueError('One indexed-table layer only; retain its original parent')
    inherited=verify_parent(parent,decoder)
    if 'code_data_filter' in scan and not inherited:raise ValueError('Malformed inherited table declaration')
    functions=json.loads(files['functions.json'])
    excluded=find_tables(raw,sections_from_container(raw,profile),functions,json.loads(files['xrefs.json']),decoder)
    if not excluded:raise ValueError('No independently evidenced table seeds')
    starts=[address(item['function']['start']) for item in excluded]
    if len(set(starts))!=len(starts):raise ValueError('Duplicate table seed')
    audit=dict(status='INDEXED_TABLE_SELECTION',schema=SCHEMA,parent_identity=identity,
        source_sha256=source_hashes(),excluded=excluded,tail_trims=[],
        inherited_selection_sha256=digest(json.dumps(inherited,sort_keys=True).encode()),
        functions_before=len(functions),functions_after=len(functions)-len(excluded),
        native_engine_started=False,cold_boot_supported=False)
    return raw,profile,scan,files,functions,audit

def declaration(parent,audit_bytes,audit):
    return dict(schema=SCHEMA,parent_analysis=str(parent.resolve()),
        parent_scan_sha256=digest((parent/'scan-identity.json').read_bytes()),
        audit_sha256=digest(audit_bytes),source_sha256=audit['source_sha256'],
        excluded_entries=sorted(address(e['function']['start']) for e in audit['excluded']),
        tail_trims=[],inherited_selection_sha256=audit['inherited_selection_sha256'])

def child_scan(parent_scan,files,selection):
    scan=dict(parent_scan)
    scan.pop('code_data_filter',None)
    scan['files']={name:digest(data) for name,data in files.items()}
    scan['indexed_table_filter']=selection
    return scan

def verify_derivation(analysis,decoder):
    analysis=analysis.resolve()
    raw,profile,scan,files,_=load_inputs(analysis)
    if 'indexed_table_filter' not in scan:return verify_parent(analysis,decoder)
    declared=scan['indexed_table_filter']
    if ('code_data_filter' in scan or not isinstance(declared,dict) or declared.get('schema')!=SCHEMA):
        raise ValueError('Conflicting or unsupported table selection')
    parent=Path(declared['parent_analysis']).resolve()
    if parent==analysis:raise ValueError('Self-referential table selection')
    original,parent_profile,parent_scan,before,functions,audit=findings(parent,decoder)
    audit_bytes=(analysis/'code-data-audit.json').read_bytes()
    if (raw!=original or profile!=parent_profile or json.loads(audit_bytes)!=audit or
        any((analysis/name).read_bytes()!=(parent/name).read_bytes() for name in ('input-profile.json','aot-index-used.py'))):
        raise ValueError('Indexed-table evidence differs from preserved parent')
    expected_selection=declaration(parent,audit_bytes,audit)
    expected_files=filtered_files(before,functions,set(expected_selection['excluded_entries']))
    if declared!=expected_selection or files!=expected_files or scan!=child_scan(parent_scan,expected_files,expected_selection):
        raise ValueError('Indexed-table selection differs from reproduced evidence')
    return declared

def derive(parent,output,deps):
    sys.path.insert(0,str(deps.resolve()))
    import capstone
    parent=parent.resolve();output=output.resolve()
    if output.exists() or output.is_relative_to(parent):raise ValueError('Fresh selection outside the preserved parent required')
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
    raw,profile,scan,files,functions,audit=findings(parent,decoder)
    audit_bytes=(json.dumps(audit,indent=2)+'\n').encode()
    selection=declaration(parent,audit_bytes,audit)
    expected=filtered_files(files,functions,set(selection['excluded_entries']))
    if load_inputs(parent)[:4]!=(raw,profile,scan,files) or source_hashes()!=audit['source_sha256']:
        raise ValueError('Inputs changed while deriving table selection')
    output.mkdir(parents=True);(output/'disasm').mkdir()
    for name in ('input-profile.json','fnv-analysis.xbe','aot-index-used.py'):
        shutil.copyfile(parent/name,output/name)
    for name,data in expected.items():(output/'disasm'/name).write_bytes(data)
    (output/'code-data-audit.json').write_bytes(audit_bytes)
    (output/'scan-identity.json').write_text(json.dumps(child_scan(scan,expected,selection),indent=2)+'\n',encoding='utf-8')
    return selection

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('analysis','output','python-deps'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();result=derive(a.analysis,a.output,a.python_deps)
    print('DERIVED',len(result['excluded_entries']),'extra table seeds; engine_started=False')

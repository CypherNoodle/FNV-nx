"""Compose table-only selections and reject rehashed or inherited tampering."""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
from pe_fixture import write_pe

def digest(data):return hashlib.sha256(data).hexdigest()

def test(deps,temp_dir):
    sys.path[:0]=[str(Path(__file__).resolve().parents[1]/'tools'),str(deps.resolve())]
    import capstone
    from aot_code_data import run
    from aot_table_selection import derive,verify_derivation
    from prepare_aot_image import prepare
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
    base=0x401000;table=base+0x82;selectors=table+12;seed=base+0x500
    raw=bytearray(b'\xcc'*0xe00)
    code=bytes.fromhex('558bec837dfc070f87330000008b4dfc0fb691')+struct.pack('<I',selectors)+bytes.fromhex('ff2495')+struct.pack('<I',table)
    raw[:len(code)]=code
    for at in (0x30,0x40,0x50):raw[at:at+6]=b'\xb8'+struct.pack('<I',at)+b'\xc3'
    raw[0x7f]=0xc3;raw[0x80:0x82]=bytes.fromhex('8bff')
    struct.pack_into('<III',raw,0x82,base+0x30,base+0x40,base+0x50)
    raw[selectors-base:selectors-base+8]=bytes([0,1,2,1,0,2,2,1])
    raw[0x300:0x30a]=bytes.fromhex('558becff2485')+struct.pack('<I',seed+2)
    for at in (0x320,0x330,0x340):raw[at:at+6]=b'\xb8'+struct.pack('<I',at)+b'\xc3'
    raw[0x500:0x502]=bytes.fromhex('8bff');struct.pack_into('<III',raw,0x502,base+0x320,base+0x330,base+0x340)
    def function(start,end,method,prologue):
        return dict(start=hex(start),end=hex(end),size=end-start,section='.text',
            detection_method=method,has_prologue=prologue,called_by=[],calls_to=[],num_instructions=1)
    functions=[function(base,base+0x80,'prologue',True),function(base+0x80,base+0xa0,'gap_prologue',False),
               function(base+0x300,base+0x380,'prologue',True),function(seed,seed+18,'gap_prologue',False)]
    with tempfile.TemporaryDirectory(prefix='indexed-table-',dir=temp_dir) as tmp:
        root=Path(tmp);exe=root/'fixture.exe';write_pe(exe)
        pe=bytearray(exe.read_bytes());pe[0x200:]=raw;struct.pack_into('<I',pe,0x98+224+36,0x60000020);exe.write_bytes(pe)
        source=root/'source';prepare(exe,source,digest(pe));(source/'disasm').mkdir()
        for name,value in {'functions.json':functions,'xrefs.json':[],'labels.json':[],
                           'summary.json':{'binary':'fixture'}}.items():
            (source/'disasm'/name).write_text(json.dumps(value))
        wrapper=source/'aot-index-used.py';wrapper.write_text('raise RuntimeError("not executed")\n')
        scan=dict(input_sha256=digest((source/'fnv-analysis.xbe').read_bytes()),sdk_sha256='fixture',
            index_wrapper_sha256=digest(wrapper.read_bytes()),
            files={p.name:digest(p.read_bytes()) for p in (source/'disasm').iterdir()})
        (source/'scan-identity.json').write_text(json.dumps(scan))
        parent=root/'parent';old=run(source,root/'old.json',deps,parent)
        assert len(old['excluded'])==1
        snapshot={p.relative_to(parent).as_posix():p.read_bytes() for p in parent.rglob('*') if p.is_file()}
        child=root/'child';selection=derive(parent,child,deps)
        assert selection['excluded_entries']==[seed] and selection['tail_trims']==[]
        assert verify_derivation(child,decoder)==selection
        kept=json.loads((child/'disasm/functions.json').read_text())
        assert kept==[functions[0],functions[2]]
        assert snapshot=={p.relative_to(parent).as_posix():p.read_bytes() for p in parent.rglob('*') if p.is_file()}
        original_scan=(child/'scan-identity.json').read_bytes();original_audit=(child/'code-data-audit.json').read_bytes()
        checks=2
        def reject():
            nonlocal checks
            try:verify_derivation(child,decoder)
            except ValueError:checks+=1
            else:raise AssertionError('Tampered composition accepted')
        altered=json.loads(original_scan);altered['files']['functions.json']=digest(b'[]')
        (child/'disasm/functions.json').write_bytes(b'[]');(child/'scan-identity.json').write_text(json.dumps(altered));reject()
        (child/'disasm/functions.json').write_bytes((json.dumps(kept,indent=2)+'\n').encode())
        altered=json.loads(original_scan);audit=json.loads(original_audit);audit['excluded']=[]
        blob=json.dumps(audit).encode();(child/'code-data-audit.json').write_bytes(blob)
        altered['indexed_table_filter']['audit_sha256']=digest(blob);(child/'scan-identity.json').write_text(json.dumps(altered));reject()
        (child/'code-data-audit.json').write_bytes(original_audit)
        for change in ('cycle','conflict','schema','empty','null'):
            altered=json.loads(original_scan)
            if change=='cycle':altered['indexed_table_filter']['parent_analysis']=str(child)
            elif change=='conflict':altered['code_data_filter']={}
            elif change=='schema':altered['indexed_table_filter']['schema']='unknown'
            elif change=='empty':altered['indexed_table_filter']={}
            elif change=='null':altered['indexed_table_filter']=None
            (child/'scan-identity.json').write_text(json.dumps(altered));reject()
        (child/'scan-identity.json').write_bytes(original_scan)
        assert verify_derivation(child,decoder)==selection
        before=(parent/'disasm/functions.json').read_bytes();(parent/'disasm/functions.json').write_bytes(b'[]');reject()
        (parent/'disasm/functions.json').write_bytes(before)
        assert verify_derivation(child,decoder)==selection
    print('PASS:',checks,'composition/integrity checks; prior exclusions and retained function records preserved')

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--python-deps',type=Path,required=True);p.add_argument('--temp-dir',type=Path)
    a=p.parse_args();test(a.python_deps,a.temp_dir)

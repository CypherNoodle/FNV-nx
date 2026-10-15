"""Guarded table data vs. real code, adversarial entries and immutable derivation."""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
from pe_fixture import write_pe


def test(deps, temp_dir=None):
    pc=Path(__file__).resolve().parents[1]
    sys.path[:0]=[str(pc/'tools'),str(deps.resolve())]
    import capstone
    from aot_code_data import audit_tables, run, verify_derivation, sha, load_inputs
    from verify_aot_code_data import function_hashes, dispatch
    from prepare_aot_image import prepare
    base=0x401000;table=base+0x82;selectors=table+12
    raw=bytearray(b'\xcc'*0xe00)
    # Known prologue, bounded stack index, adjacent two-level dispatch.
    code=bytes.fromhex('558bec837dfc070f87330000008b4dfc0fb691')+struct.pack('<I',selectors)+bytes.fromhex('ff2495')+struct.pack('<I',table)
    raw[:len(code)]=code
    for offset in (0x30,0x40,0x50): raw[offset:offset+6]=b'\xb8'+struct.pack('<I',offset)+b'\xc3'
    raw[0x7f]=0xc3;raw[0x80:0x82]=bytes.fromhex('8bff')
    struct.pack_into('<III',raw,table-base,base+0x30,base+0x40,base+0x50)
    raw[selectors-base:selectors-base+8]=bytes([0,1,2,1,0,2,2,1])
    section=dict(start=base,end=base+len(raw),stored=len(raw),offset=0,executable=True)
    owner=dict(start=hex(base),end=hex(base+0x80),detection_method='prologue',has_prologue=True,section='.text',called_by=[],calls_to=[])
    fake=dict(start=hex(base+0x80),end=hex((selectors+8+15)&~15),detection_method='gap_prologue',has_prologue=False,section='.text',called_by=[],calls_to=[])
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
    def audit(blob=raw, funcs=None, refs=None):
        return audit_tables(bytes(blob),[section],funcs or [owner,fake],refs or [],decoder)
    good=audit();assert len(good['tables'])==1 and len(good['excluded'])==1,good
    rejected=0
    def no_exclusion(blob=raw,funcs=None,refs=None):
        nonlocal rejected
        assert not audit(blob,funcs,refs)['excluded'];rejected+=1
    # Wrong bound, signed comparison, wrong index, unbalanced/no ret, OOB selectors.
    for offset,value in ((6,255),(8,0x8f),(14,0x55),(0x7f,0x90)):
        bad=bytearray(raw);bad[offset]=value;no_exclusion(bad)
    for target in (base+0x31,table,0x500000):
        bad=bytearray(raw);struct.pack_into('<I',bad,table-base,target);no_exclusion(bad)
    bad=bytearray(raw);struct.pack_into('<I',bad,17,base+len(raw)-4);no_exclusion(bad)
    # Contradictory branch, callback-like address, bypassed guard, strong entry.
    for target,kind in ((table,'jump'),(base+0x80,'data_imm'),(base+12,'jump'),(selectors,'call')):
        no_exclusion(refs=[dict(type=kind,**{'from':hex(base+0x60),'to':hex(target)})])
    strong=deepcopy(fake);strong['start']=hex(table);strong['detection_method']='call_target'
    no_exclusion(funcs=[owner,fake,strong])
    called=deepcopy(fake);called['called_by']=[hex(base)];no_exclusion(funcs=[owner,called])
    weak=deepcopy(owner);weak['detection_method']='gap_prologue';no_exclusion(funcs=[weak,fake])
    bad=bytearray(raw);bad[0x80:0x82]=bytes.fromhex('558b');no_exclusion(bad)
    # A genuine adjacent hotpatch prologue after data is never swallowed.
    real=deepcopy(owner);real['start']=hex(base+0xa0);real['end']=hex(base+0xa8)
    both=audit(funcs=[owner,fake,real]);assert both['functions_after']==2 and len(both['excluded'])==1
    # Register-index form, with shorter four-instruction guard.
    reg=bytearray(raw);reg[:len(code)]=b'\x90'*len(code)
    reg_code=bytes.fromhex('558bec83f9070f87340000000fb691')+struct.pack('<I',selectors)+bytes.fromhex('ff2495')+struct.pack('<I',table)
    reg[:len(reg_code)]=reg_code
    assert len(audit(reg)['excluded'])==1
    # Build a real synthetic analysis container for integrity/selection tests.
    with tempfile.TemporaryDirectory(prefix='fnv-code-data-',dir=temp_dir) as tmp:
        root=Path(tmp);exe=root/'fixture.exe';write_pe(exe)
        c='/* void fake(void) { } */\nvoid sub_00401000(void) {\n const char *p="} sub_0041809A";\n if (p) { sub_00402000(); }\n}\n'
        hashes,refs=function_hashes(c)
        assert set(hashes)=={'sub_00401000'} and refs['sub_00401000']=={'sub_00402000'}
        assert function_hashes(c.replace('} sub_0041809A','changed string'))[0]!=hashes
        for broken in (c+c,'void unfinished(void) {'):
            try:function_hashes(broken)
            except ValueError:pass
            else:raise AssertionError('Malformed C body census accepted')
        d=root/'dispatch.c';d.write_text('/* { 0x0041809Au, (recomp_func_t)sub_0041809A } */\n{ 0x00401000u, (recomp_func_t)sub_00401000 },')
        assert dispatch(d)=={0x401000:'sub_00401000'}
        pe=bytearray(exe.read_bytes());pe[0x200:]=raw
        struct.pack_into('<I',pe,0x98+224+36,0x60000020);exe.write_bytes(pe)
        source=root/'source';prepare(exe,source,sha(pe));(source/'disasm').mkdir()
        files={'functions.json':[owner,fake],'xrefs.json':[],'summary.json':{'binary':'fnv-analysis.xbe'},'labels.json':[]}
        for name,value in files.items():(source/'disasm'/name).write_text(json.dumps(value))
        (source/'aot-index-used.py').write_text('raise RuntimeError("identity evidence only")\n')
        scan=dict(input_sha256=sha((source/'fnv-analysis.xbe').read_bytes()),sdk_sha256='fixture',
            index_wrapper_sha256=sha((source/'aot-index-used.py').read_bytes()),
            files={n:sha((source/'disasm'/n).read_bytes()) for n in files})
        (source/'scan-identity.json').write_text(json.dumps(scan))
        before={p.relative_to(source).as_posix():sha(p.read_bytes()) for p in source.rglob('*') if p.is_file()}
        derived=root/'derived';result=run(source,root/'report.json',deps,derived)
        assert len(result['excluded'])==1 and verify_derivation(derived,decoder)['excluded_entries']==[base+0x80]
        assert before=={p.relative_to(source).as_posix():sha(p.read_bytes()) for p in source.rglob('*') if p.is_file()}
        # Self-consistent hash tampering must still fail parent reproduction.
        target=derived/'disasm/functions.json';target.write_text('[]')
        ds=json.loads((derived/'scan-identity.json').read_text());ds['files']['functions.json']=sha(target.read_bytes())
        (derived/'scan-identity.json').write_text(json.dumps(ds))
        try: verify_derivation(derived,decoder)
        except ValueError: pass
        else: raise AssertionError('Rehashed selection tampering accepted')
        target=source/'disasm/xrefs.json';target.write_text('[{}]')
        try: load_inputs(source)
        except ValueError: pass
        else: raise AssertionError('Changed accepted scan accepted')
    print('PASS: guarded stack/register switches,',rejected,'adversarial rejections, protected real entry, unchanged parent and rehashed-tamper rejection')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--python-deps',type=Path,required=True);p.add_argument('--temp-dir',type=Path)
    a=p.parse_args();test(a.python_deps,a.temp_dir)

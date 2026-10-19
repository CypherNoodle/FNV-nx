"""Audit a complete normal EH3 cookie-frame helper pair, not exception dispatch."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
from audit_seh_helpers import body
from audit_startup import audit


def describe(analysis,prolog,epilog):
    profile_bytes=(analysis/'input-profile.json').read_bytes();p=json.loads(profile_bytes);image=(analysis/'fnv-analysis.xbe').read_bytes()
    digest=lambda b:hashlib.sha256(b).hexdigest()
    if not p['analysis_only'] or digest(image)!=p['analysis_container_sha256']:raise ValueError('Analysis identity mismatch')
    db={int(f['start'],0):f for f in json.loads((analysis/'disasm/functions.json').read_text())}
    text=next(s for s in p['sections'] if s['name']=='.text');low=p['image_base']+text['rva'];high=low+text['virtual_bytes']
    helpers=[]
    for kind,at in [('prolog',prolog),('epilog',epilog)]:
        if at not in db or db[at].get('section')!='.text' or not low<=at<high:raise ValueError('Require an existing text entry')
        rows,raw=body(image,p['image_base'],at)
        if at+len(raw)>high:raise ValueError('Helper crosses text')
        extra={}
        if kind=='prolog':
            from capstone.x86_const import X86_OP_MEM
            if len(rows)!=17 or len(rows[9].operands)!=2 or rows[9].operands[1].type!=X86_OP_MEM:raise ValueError('Unsupported EH3 prolog')
            mem=rows[9].operands[1].mem
            if mem.base or mem.index or mem.segment:raise ValueError('Expected absolute cookie')
            cookie=mem.disp&0xffffffff
            if not any(s['name'] in ('.data','.rdata') and p['image_base']+s['rva']<=cookie and cookie+4<=p['image_base']+s['rva']+s['virtual_bytes'] for s in p['sections']):raise ValueError('Cookie outside data')
            expected=[('push','eax'),('push','dword ptr fs:[0]'),('lea','eax, [esp + 0xc]'),('sub','esp, dword ptr [esp + 0xc]'),
                      ('push','ebx'),('push','esi'),('push','edi'),('mov','dword ptr [eax], ebp'),('mov','ebp, eax'),
                      ('mov','eax, dword ptr ['+hex(cookie)+']'),('xor','eax, ebp'),('push','eax'),('push','dword ptr [ebp - 4]'),
                      ('mov','dword ptr [ebp - 4], 0xffffffff'),('lea','eax, [ebp - 0xc]'),('mov','dword ptr fs:[0], eax'),('ret','')]
            extra=dict(cookie=cookie,publish_address=rows[8].address)
        else:
            expected=[('mov','ecx, dword ptr [ebp - 0xc]'),('mov','dword ptr fs:[0], ecx'),('pop','ecx'),('pop','edi'),('pop','edi'),
                      ('pop','esi'),('pop','ebx'),('mov','esp, ebp'),('pop','ebp'),('push','ecx'),('ret','')]
        if [(i.mnemonic,i.op_str) for i in rows]!=expected:raise ValueError('Unsupported EH3 '+kind+' semantics')
        helpers.append(dict(kind=kind,address=at,bytes=len(raw),body_sha256=digest(raw),**extra))
    if prolog==epilog:raise ValueError('Require distinct helpers')
    return dict(status='MATCHED_NORMAL_EH3_FRAME_PATTERN',abi='msvc-eh3-cookie-normal-frame-v1',helpers=helpers,
        input_profile_sha256=digest(profile_bytes),analysis_container_sha256=digest(image),audit_source_sha256=digest(Path(__file__).read_bytes()),
        exception_dispatch_implemented=False,native_engine_started=False)


def verify(analysis,report):
    prolog,epilog=report['helpers'];current=describe(analysis,prolog['address'],epilog['address'])
    if current!=report:raise ValueError('EH3 helper audit changed')
    return current


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('exe','analysis','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    for n in ('prolog','epilog'):p.add_argument('--'+n,type=lambda s:int(s,0),required=True)
    a=p.parse_args();sys.path.insert(0,str(a.python_deps.resolve()));identity=audit(a.exe,a.analysis)
    result=describe(a.analysis,a.prolog,a.epilog)
    if audit(a.exe,a.analysis)!=identity:raise ValueError('Inputs changed')
    if a.output.exists():raise ValueError('Use a fresh private report')
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n');print(result['status'])

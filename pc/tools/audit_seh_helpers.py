"""Verify explicit MSVC-style normal-frame helpers; never execute guest code.

Matches a whole straight-line body, not isolated byte markers. This does not
implement exception dispatch, prove callers reachable, or establish cold boot.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
from audit_startup import audit


def sha(data):return hashlib.sha256(data).hexdigest()


def body(image, base, address, maximum=128):
    if len(image)<4096 or image[:4]!=b'XBEH':raise ValueError('Invalid analysis container')
    count,table=struct.unpack_from('<II',image,0x11c);table-=base
    if not 0<count<=32 or not 0<=table<=4096-count*56:raise ValueError('Invalid section table')
    matches=[]
    for i in range(count):
        _,start,size,offset,length=struct.unpack_from('<5I',image,table+i*56)
        if length>size or offset<4096 or offset+length>len(image):raise ValueError('Invalid section backing')
        if start<=address<start+length:matches.append((start,offset,length))
    if len(matches)!=1:raise ValueError('Helper requires unique file backing')
    start,offset,length=matches[0];delta=address-start
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True
    rows=[];used=0
    for ins in decoder.disasm(image[offset+delta:offset+min(length,delta+maximum)],address):
        rows.append(ins);used+=ins.size
        if ins.mnemonic=='ret':return rows,image[offset+delta:offset+delta+used]
    raise ValueError('No bounded helper return')


def classify(rows,kind):
    """Exact instruction semantics for one audited ABI, with two variable addresses."""
    from capstone.x86_const import X86_OP_IMM,X86_OP_MEM
    actual=[(i.mnemonic,i.op_str) for i in rows]
    if kind=='prolog':
        if len(rows)!=21:raise ValueError('Unsupported prolog instruction count')
        handler,cookie=rows[0],rows[9]
        if handler.mnemonic!='push' or len(handler.operands)!=1 or handler.operands[0].type!=X86_OP_IMM:
            raise ValueError('Expected handler immediate')
        if cookie.mnemonic!='mov' or len(cookie.operands)!=2 or cookie.operands[1].type!=X86_OP_MEM:
            raise ValueError('Expected cookie address')
        mem=cookie.operands[1].mem
        if mem.base or mem.index or mem.segment:raise ValueError('Cookie must use absolute memory')
        expected=[('push',handler.op_str),('push','dword ptr fs:[0]'),
            ('mov','eax, dword ptr [esp + 0x10]'),('mov','dword ptr [esp + 0x10], ebp'),
            ('lea','ebp, [esp + 0x10]'),('sub','esp, eax'),('push','ebx'),('push','esi'),('push','edi'),
            ('mov','eax, dword ptr ['+hex(mem.disp & 0xffffffff)+']'),
            ('xor','dword ptr [ebp - 4], eax'),('xor','eax, ebp'),('push','eax'),
            ('mov','dword ptr [ebp - 0x18], esp'),('push','dword ptr [ebp - 8]'),
            ('mov','eax, dword ptr [ebp - 4]'),('mov','dword ptr [ebp - 4], 0xfffffffe'),
            ('mov','dword ptr [ebp - 8], eax'),('lea','eax, [ebp - 0x10]'),
            ('mov','dword ptr fs:[0], eax'),('ret','')]
        # Capstone reports 21 instructions for this ABI (not a marker match).
        if len(expected)!=len(rows):raise ValueError('Unsupported prolog instruction count')
        extra=dict(handler=handler.operands[0].imm & 0xffffffff,cookie=mem.disp & 0xffffffff)
    elif kind=='epilog':
        expected=[('mov','ecx, dword ptr [ebp - 0x10]'),('mov','dword ptr fs:[0], ecx'),
            ('pop','ecx'),('pop','edi'),('pop','edi'),('pop','esi'),('pop','ebx'),
            ('mov','esp, ebp'),('pop','ebp'),('push','ecx'),('ret','')]
        extra={}
    else:raise ValueError('Unknown helper kind')
    if actual!=expected:raise ValueError('Unsupported '+kind+' semantics')
    return dict(kind=kind,address=rows[0].address,instructions=len(rows),**extra)


def describe(analysis,prolog,epilog):
    profile_bytes=(analysis/'input-profile.json').read_bytes();p=json.loads(profile_bytes)
    image=(analysis/'fnv-analysis.xbe').read_bytes()
    if not p['analysis_only'] or sha(image)!=p['analysis_container_sha256']:raise ValueError('Analysis identity mismatch')
    text=next(s for s in p['sections'] if s['name']=='.text')
    def text_address(at):return p['image_base']+text['rva']<=at<p['image_base']+text['rva']+text['virtual_bytes']
    helpers=[]
    for kind,at in [('prolog',prolog),('epilog',epilog)]:
        if not text_address(at):raise ValueError('Helper outside text')
        rows,raw=body(image,p['image_base'],at);item=classify(rows,kind)
        if at+len(raw)>p['image_base']+text['rva']+text['virtual_bytes']:raise ValueError('Helper crosses text')
        item.update(bytes=len(raw),body_sha256=sha(raw));helpers.append(item)
    if prolog==epilog or not text_address(helpers[0]['handler']):raise ValueError('Invalid helper/handler layout')
    cookie=helpers[0]['cookie']-p['image_base']
    if not any(s['name'] in ('.data','.rdata') and s['rva']<=cookie and cookie+4<=s['rva']+s['virtual_bytes'] for s in p['sections']):
        raise ValueError('Cookie outside data sections')
    if (analysis/'input-profile.json').read_bytes()!=profile_bytes or (analysis/'fnv-analysis.xbe').read_bytes()!=image:
        raise ValueError('Inputs changed')
    return dict(status='MATCHED_NORMAL_FRAME_PATTERN',abi='msvc-seh-cookie-frame-v1',
        input_profile_sha256=sha(profile_bytes),analysis_container_sha256=sha(image),
        audit_source_sha256=sha(Path(__file__).read_bytes()),helpers=helpers,
        exception_dispatch_implemented=False,native_engine_started=False)


def verify(analysis,report):
    try:prolog,epilog=report['helpers']
    except (KeyError,ValueError):raise ValueError('Require one helper pair')
    current=describe(analysis,prolog['address'],epilog['address'])
    if current!=report:raise ValueError('Helper audit changed or does not match inputs')
    return current


def generation_arguments(analysis,report):
    """Only a freshly revalidated body audit can select translator helper overrides."""
    prolog,epilog=verify(analysis,report)['helpers']
    return ['--seh-prolog',hex(prolog['address']),'--seh-epilog',hex(epilog['address'])]


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','analysis','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    for name in ('prolog','epilog'):p.add_argument('--'+name,type=lambda s:int(s,0),required=True)
    a=p.parse_args();sys.path.insert(0,str(a.python_deps.resolve()))
    identity=audit(a.exe,a.analysis)
    result=describe(a.analysis,a.prolog,a.epilog)
    if audit(a.exe,a.analysis)!=identity:raise ValueError('Executable identity changed')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    with a.output.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
    print(result['status'],'normal frame only; no exception dispatch or game execution')

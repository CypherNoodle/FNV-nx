"""Audit an explicitly bounded PC PE32 pointer table against a function list.

The caller supplies the observed table address and count. This does not infer
table boundaries or turn interior labels into functions. Reports stay private.
"""
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import struct
from audit_gap_function import audit

def table_offset(image,address,count):
    def u16(at):
        if at<0 or at+2>len(image):raise ValueError('Truncated PE')
        return struct.unpack_from('<H',image,at)[0]
    def u32(at):
        if at<0 or at+4>len(image):raise ValueError('Truncated PE')
        return struct.unpack_from('<I',image,at)[0]
    if len(image)<64 or image[:2]!=b'MZ':raise ValueError('Require PE image')
    pe=u32(0x3c)
    if image[pe:pe+4]!=b'PE\0\0' or u16(pe+4)!=0x14c:raise ValueError('Require x86 PE')
    optional=pe+24;optional_bytes=u16(pe+20);sections=u16(pe+6)
    if optional_bytes<96 or u16(optional)!=0x10b or not 1<=sections<=96:raise ValueError('Require PE32 section table')
    base=u32(optional+28);size=count*4
    if not 1<=count<=4096 or address%4 or address<base or address+size>0x100000000:raise ValueError('Invalid explicit table bounds')
    headers=optional+optional_bytes
    if headers+sections*40>len(image):raise ValueError('Truncated section table')
    for index in range(sections):
        at=headers+index*40;rva=u32(at+12);raw_bytes=u32(at+16);offset=u32(at+20);flags=u32(at+36)
        if base+rva<=address and address+size<=base+rva+raw_bytes:
            if not flags&0x40000000 or flags&(0x20000000|0x80000000):raise ValueError('Require immutable non-code table data')
            start=offset+address-base-rva
            if start<0 or start+size>len(image):raise ValueError('Table leaves raw section')
            return start
    raise ValueError('Table is outside backed PE section data')

def audit_table(image,table,count,code,base,functions):
    at=table_offset(image,table,count);pointers=struct.unpack_from('<%dI'%count,image,at)
    ranges=sorted((int(r['start'],0) if isinstance(r['start'],str) else r['start'],int(r['end'],0) if isinstance(r['end'],str) else r['end']) for r in functions)
    if not 0<=base<base+len(code)<=0x100000000:raise ValueError('Invalid code range')
    if any(not 0<=a<b<=0x100000000 for a,b in ranges):raise ValueError('Invalid existing function range')
    starts=[a for a,b in ranges];known=set(starts);gaps={};interiors={};outside=[];entries=0
    for slot,target in enumerate(pointers):
        if not base<=target<base+len(code):outside.append(slot);continue
        if target in known:entries+=1;continue
        i=bisect.bisect_right(starts,target)-1
        # Search every possible owner: the source list may contain overlaps.
        if any(a<=target<b for a,b in ranges[:i+1]):interiors.setdefault(target,[]).append(slot);continue
        gaps.setdefault(target,[]).append(slot)
    audited=[];rejected=[]
    for target,slots in sorted(gaps.items()):
        i=bisect.bisect_right(starts,target);limit=min(base+len(code),target+65536,starts[i] if i<len(starts) else base+len(code))
        try:result=audit(code,base,target,limit,functions)
        except ValueError as error:rejected.append(dict(entry=target,slots=slots,reason=str(error)))
        else:audited.append(dict(entry=target,slots=slots,audit=result))
    return dict(status='AUDITED_EXPLICIT_TABLE',table=table,count=count,known_entry_slots=entries,
                audited_gaps=audited,rejected_gaps=rejected,interior_targets=[dict(entry=a,slots=s) for a,s in sorted(interiors.items())],outside_code_slots=outside,
                image_sha256=hashlib.sha256(image).hexdigest(),code_sha256=hashlib.sha256(code).hexdigest(),
                inferred_table_boundary=False,generated_code=False,engine_started=False)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('image','code','functions','output'):p.add_argument('--'+n,type=Path,required=True)
    for n in ('base','table','count'):p.add_argument('--'+n,type=lambda s:int(s,0),required=True)
    a=p.parse_args()
    if a.output.exists():raise FileExistsError(a.output)
    result=audit_table(a.image.read_bytes(),a.table,a.count,a.code.read_bytes(),a.base,json.loads(a.functions.read_text()))
    result['functions_sha256']=hashlib.sha256(a.functions.read_bytes()).hexdigest()
    with a.output.open('x',encoding='ascii') as stream:json.dump(result,stream,indent=2);stream.write('\n')

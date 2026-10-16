"""Bounded read-only D3D9 SM2/3 CTAB metadata inspection, not execution.

Reports the declared ranges only. Does not repair them or infer dynamic bounds.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect_ctab(data):
    if not 8 <= len(data) <= 1024*1024 or len(data)%4:
        raise ValueError('invalid bytecode size')
    words=struct.unpack('<%dI'%(len(data)//4),data)
    if words[0]>>16 not in (0xfffe,0xffff) or (words[0]>>8)&255 not in (2,3):
        raise ValueError('CTAB walk requires SM2/3')
    tables=[];pos=1;ended=False
    while pos<len(words):
        token=words[pos];op=token&65535
        if op==65535:
            if token!=65535 or pos!=len(words)-1:raise ValueError('invalid END/trailing words')
            ended=True;break
        length=(token>>16)&32767 if op==65534 else (token>>24)&15
        if pos+length+1>len(words):raise ValueError('truncated token/comment')
        if op==65534 and length and words[pos+1]==0x42415443:
            tables.append(data[(pos+2)*4:(pos+length+1)*4])
        pos+=1+length
    if not ended or len(tables)>1:raise ValueError('missing END/ambiguous CTAB')
    if not tables:return {'shader_sha256':hashlib.sha256(data).hexdigest(),'constants':[],'ctab_present':False}
    table=tables[0]
    if len(table)<28:raise ValueError('truncated CTAB header')
    size,creator,version,count,offset,flags,target=struct.unpack_from('<7I',table)
    if size!=28 or version!=words[0] or count>4096 or offset>len(table) or count>(len(table)-offset)//20:
        raise ValueError('invalid CTAB constant table')
    def text(at):
        if not 0<=at<len(table):raise ValueError('CTAB string out of bounds')
        end=table.find(b'\0',at,min(at+4096,len(table)))
        if end<0:raise ValueError('unterminated CTAB string')
        return table[at:end].decode('ascii')
    records=[]
    for index in range(count):
        name,kind,first,n,reserved,typ,default=struct.unpack_from('<I4H2I',table,offset+index*20)
        if kind>3 or n==0 or typ>len(table)-16 or (default and default>=len(table)):
            raise ValueError('invalid CTAB constant record')
        cls,scalar,rows,columns,elements,struct_members,member_info=struct.unpack_from('<6HI',table,typ)
        records.append({'name':text(name),'register_set':kind,'first':first,'count':n,'reserved':reserved,
            'class':cls,'scalar_type':scalar,'rows':rows,'columns':columns,'elements':elements,
            'struct_members':struct_members,'member_info_offset':member_info})
    return {'shader_sha256':hashlib.sha256(data).hexdigest(),'ctab_present':True,
            'creator':text(creator),'target':text(target),'flags':flags,'constants':records,
            'metadata_only':True,'ranges_repaired':False}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('shader',type=Path);p.add_argument('--output',type=Path)
    args=p.parse_args();report=json.dumps(inspect_ctab(args.shader.read_bytes()),indent=2)+'\n'
    if args.output:
        with args.output.open('x',encoding='utf-8') as f:f.write(report)
    else:print(report,end='')

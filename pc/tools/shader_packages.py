"""Bounded read-only SDP inventory; optional private bytecode export by SHA256.

Checks container/record/version/end-marker structure, not shader semantics.
No code translation, driver compilation or rendering is implied.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

MAX_PACKAGE=64*1024*1024

def digest(data):return hashlib.sha256(data).hexdigest()

def parse_package(data):
    if len(data)<12 or len(data)>MAX_PACKAGE:raise ValueError('SDP size outside bounds')
    magic,count,size=struct.unpack_from('<3I',data)
    if magic!=100 or size!=len(data)-12 or count>size//268:
        raise ValueError('Invalid SDP header/count')
    records=[];position=12
    for index in range(count):
        if position+260>len(data):raise ValueError('Truncated SDP record')
        name_bytes=data[position:position+256];end=name_bytes.find(b'\0')
        if end<=0:raise ValueError('Unterminated or empty shader name')
        name=name_bytes[:end].decode('ascii')
        if any(ord(c)<32 or ord(c)>126 for c in name):raise ValueError('Invalid shader name')
        size=struct.unpack_from('<I',data,position+256)[0];position+=260
        if size<8 or size%4 or size>len(data)-position:raise ValueError('Invalid bytecode bounds')
        code=data[position:position+size];version=struct.unpack_from('<I',code)[0]
        stage={0xFFFE:'vertex',0xFFFF:'pixel'}.get(version>>16)
        major,minor=(version>>8)&255,version&255
        if not stage or major not in (1,2,3) or struct.unpack_from('<I',code,size-4)[0]!=0xFFFF:
            raise ValueError('Invalid stage/version/end marker')
        records.append(dict(index=index,name=name,offset=position,bytes=size,stage=stage,
            major=major,minor=minor,sha256=digest(code),code=code))
        position+=size
    if position!=len(data):raise ValueError('Trailing SDP bytes')
    return records

def inventory(directory,output,export=False):
    if output.exists():raise ValueError('Use a fresh private output directory')
    files=sorted(directory.glob('*.sdp'))
    if not files:raise ValueError('No SDP files')
    frozen={};unique={};packages=[];versions=Counter()
    for path in files:
        data=path.read_bytes();frozen[path]=digest(data);records=parse_package(data)
        for record in records:
            code=record.pop('code');previous=unique.setdefault(record['sha256'],code)
            if previous!=code:raise ValueError('Bytecode digest collision')
            versions[f"{record['stage']}_{record['major']}_{record['minor']}"]+=1
        packages.append(dict(file=path.name,bytes=len(data),sha256=frozen[path],records=records))
    if any(digest(p.read_bytes())!=h for p,h in frozen.items()):raise ValueError('Inputs changed')
    result=dict(status='PASS',packages=packages,total_records=sum(len(p['records']) for p in packages),
        unique_bytecodes=len(unique),versions=dict(sorted(versions.items())),
        structural_only=True,translated=False,native_engine_started=False)
    output.mkdir(parents=True)
    if export:
        raw=output/'bytecode';raw.mkdir()
        for name,code in unique.items():(raw/(name+'.bin')).write_bytes(code)
    (output/'inventory.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--shaders',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--export-bytecode',action='store_true');a=p.parse_args()
    r=inventory(a.shaders,a.output,a.export_bytecode)
    print(json.dumps({k:v for k,v in r.items() if k!='packages'},indent=2))

"""Read-only audit of an explicitly bounded function-pointer table.

Caller must establish the table's use separately. Membership in the existing
analysis database proves neither callable ABI nor a valid cold entry. Reports
contain title-specific addresses and must stay private.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from audit_install import pe_info
from audit_startup import audit


def inspect(exe,analysis,start,end):
    if start&3 or end&3 or not 4<=end-start<=1048576:raise ValueError('Aligned nonempty table, at most 1 MiB')
    identity=audit(exe,analysis);raw=exe.read_bytes();pe=pe_info(exe)
    database=analysis/'disasm/functions.json';db_bytes=database.read_bytes()
    functions={int(f['start'],0):f for f in json.loads(db_bytes)}
    begin=start-pe['image_base'];finish=end-pe['image_base']
    section=next((s for s in pe['sections'] if s['name'] in ('.rdata','.data') and s['rva']<=begin<finish<=s['rva']+s['raw_bytes']),None)
    if section is None:raise ValueError('Table must be entirely in backed data of the original PE')
    offset=section['raw_offset']+begin-section['rva'];data=raw[offset:offset+end-start]
    values=list(struct.unpack('<'+'I'*(len(data)//4),data));roots=list(dict.fromkeys(v for v in values if v))
    text=next(s for s in pe['sections'] if s['name']=='.text')
    unknown=[v for v in roots if v not in functions or functions[v].get('section')!='.text' or
             not pe['image_base']+text['rva']<=v<pe['image_base']+text['rva']+text['virtual_bytes']]
    if audit(exe,analysis)!=identity or database.read_bytes()!=db_bytes:raise ValueError('Inputs changed during audit')
    return dict(status='KNOWN_TABLE_ENTRIES' if not unknown else 'UNKNOWN_ENTRIES',identity=identity,
        start=start,end=end,section=section['name'],slots=len(values),nonzero=sum(v!=0 for v in values),unique_roots=len(roots),roots=roots,
        unknown_roots=unknown,table_sha256=hashlib.sha256(data).hexdigest(),database_sha256=hashlib.sha256(db_bytes).hexdigest(),
        source_sha256={str(p.resolve()):hashlib.sha256(p.read_bytes()).hexdigest() for p in (Path(__file__),Path(__file__).with_name('audit_install.py'),Path(__file__).with_name('audit_startup.py'))},
        abi_verified=False,execution_verified=False,cold_entry_verified=False)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','analysis','output'):p.add_argument('--'+name,type=Path,required=True)
    for name in ('start','end'):p.add_argument('--'+name,type=lambda s:int(s,0),required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Use a fresh private report path')
    result=inspect(a.exe.resolve(),a.analysis.resolve(),a.start,a.end)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('roots','unknown_roots','identity','source_sha256')},indent=2))
    if result['unknown_roots']:raise SystemExit(1)

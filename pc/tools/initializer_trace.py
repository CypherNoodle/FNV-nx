"""Validate every nonzero initializer slot, including repeated callbacks."""
import hashlib
import struct
from audit_install import pe_info


def table_entries(exe,table):
    raw=exe.read_bytes();pe=pe_info(exe)
    if hashlib.sha256(raw).hexdigest()!=pe['sha256'] or pe['sha256']!=table['identity']['executable_sha256']:
        raise ValueError('Initializer EXE identity mismatch')
    start,end=table['start'],table['end'];begin=start-pe['image_base'];finish=end-pe['image_base']
    if start&3 or end&3 or not 4<=end-start<=1048576:raise ValueError('Invalid initializer bounds')
    section=next((s for s in pe['sections'] if s['name'] in ('.rdata','.data') and
                  s['rva']<=begin<finish<=s['rva']+s['raw_bytes']),None)
    if section is None:raise ValueError('Initializer table is not backed data')
    offset=section['raw_offset']+begin-section['rva'];data=raw[offset:offset+end-start]
    if hashlib.sha256(data).hexdigest()!=table['table_sha256']:raise ValueError('Initializer table bytes changed')
    values=list(struct.unpack('<'+'I'*(len(data)//4),data));roots=list(dict.fromkeys(v for v in values if v))
    if (len(values)!=table['slots'] or sum(bool(v) for v in values)!=table['nonzero'] or
            len(roots)!=table['unique_roots'] or roots!=table['roots']):
        raise ValueError('Initializer inventory disagrees with original bytes')
    if exe.read_bytes()!=raw:raise ValueError('Initializer EXE changed during inspection')
    return [(start+i*4,value) for i,value in enumerate(values) if value]


def check_trace(table,entries,before,after):
    """Rows contain cursor/end/target and before/after caller register state."""
    if not isinstance(before,list) or not isinstance(after,list):raise ValueError('Malformed initializer trace')
    for rows,width in ((before,7),(after,6)):
        if any(not isinstance(r,list) or len(r)!=width or
               any(type(v) is not int or not 0<=v<=0xffffffff for v in r) for r in rows):
            raise ValueError('Malformed initializer register row')
    expected=len(entries)
    sequence=(len(before)==expected and all((r[0],r[2])==entry and r[1]==table['end']
                                           for r,entry in zip(before,entries)))
    returns=(len(before)==len(after)==expected and all(
        b[0]==a[0] and b[1]==a[1] and b[3]==a[2] and b[4]==a[3] and
        b[5]+4==a[4] and b[6]==a[5] for b,a in zip(before,after)))
    return dict(schema='fnv-initializer-trace-v1',table_sha256=table['table_sha256'],
                expected_nonzero_slots=expected,calls_before=len(before),calls_after=len(after),
                exact_slot_target_sequence=sequence,caller_return_state_preserved=returns,
                complete=sequence and returns)

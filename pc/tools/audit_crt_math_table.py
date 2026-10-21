"""Read-only evidence for unaligned x87 CRT classification dispatch tables.

An original data pointer alone does not establish a callable entry. Require
the descriptor setup, indexed path to a verified selector dispatcher, exact
selector domain and original data bytes. Reports contain private game data;
this tool does not register, translate or modify functions.
"""
import argparse
from collections import deque
import hashlib
import json
from pathlib import Path
import struct
import sys


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def number(v):return int(v,0) if isinstance(v,str) else v


def recover_entries(decoder,read,functions,roots):
    """Bounded direct CFG closure rooted only in verified descriptor slots.

    New calls, indirect transfers, overlapping decodes and non-boundary
    interior targets reject. Terminal shared tails may be separate entries.
    This evidence still needs translation/ABI verification before production.
    """
    todo=deque(roots);entries={};instructions={};limits=sorted(functions)
    def owners(at):return [(s,number(f['end'])) for s,f in functions.items() if s<=at<number(f['end'])]
    while todo:
        entry=todo.popleft()
        if entry in functions or entry in entries:continue
        if len(entries)>=32:raise ValueError('Descriptor CFG entry limit')
        enclosing=owners(entry)
        for start,end in enclosing:
            if end-start>65536:raise ValueError('Owner decode limit')
            if not any(i.address==entry for i in decoder.disasm(read(start,end-start),start)):
                raise ValueError('Descriptor CFG enters an existing instruction')
        upper=min([entry+4096,*[at for at in limits if at>entry],*[end for _,end in enclosing]])
        pending=deque([entry]);visited={};external=set();terminals=0
        while pending:
            at=pending.popleft()
            if at in visited:continue
            if at in functions and at!=entry:external.add(at);terminals+=1;continue
            if not entry<=at<upper:
                external.add(at);todo.append(at);terminals+=1;continue
            row=next(decoder.disasm(read(at,15),at,count=1),None)
            if row is None or at+row.size>upper:raise ValueError('Unbounded or undecodable descriptor handler')
            if len(visited)>=512 or len(instructions)>=8192:raise ValueError('Descriptor CFG instruction limit')
            for pc,size in instructions.items():
                if at!=pc and at<pc+size and pc<at+row.size:raise ValueError('Overlapping descriptor instruction decodes')
            instructions[at]=row.size;visited[at]=row
            if row.mnemonic=='ret':terminals+=1;continue
            if row.mnemonic in ('int3','ud2','hlt','iret','iretd') or row.mnemonic.startswith('loop'):
                raise ValueError('Unsupported descriptor terminal/control flow')
            if row.mnemonic=='call' or row.mnemonic.startswith('j'):
                try:target=int(row.op_str,0)
                except ValueError:raise ValueError('Indirect descriptor handler transfer')
                if row.mnemonic=='call':
                    if target not in functions:raise ValueError('New unaudited descriptor call')
                    external.add(target)
                else:
                    pending.append(target)
                    if row.mnemonic=='jmp':continue
            pending.append(at+row.size)
        if not terminals:raise ValueError('No bounded terminal in descriptor handler')
        end=max(at+row.size for at,row in visited.items())
        # The translator consumes a contiguous body. Reject unreachable holes
        # rather than allowing a linear decode to import intervening data.
        cursor=entry
        for at,row in sorted(visited.items()):
            if at!=cursor:raise ValueError('Noncontiguous descriptor CFG requires a block-aware translator')
            cursor=at+row.size
        entries[entry]=dict(start=entry,end=end,bytes_sha256=hashlib.sha256(read(entry,end-entry)).hexdigest(),
                            instructions=len(visited),external_targets=sorted(external),
                            original_owner_boundary=bool(enclosing))
    return [entries[at] for at in sorted(entries)]


def inspect_table(decoder,read,functions,setup,dispatcher):
    """Pure bounded inspection; reader must enforce mapped original bytes."""
    def decode(start,end):
        code=read(start,end-start);rows=list(decoder.disasm(code,start))
        if not rows or sum(i.size for i in rows)!=len(code):raise ValueError('Incomplete dispatch decode')
        return rows
    if setup not in functions or dispatcher not in functions:raise ValueError('Setup/dispatcher must be indexed')
    start=decode(setup,setup+10)
    if len(start)!=2 or start[0].mnemonic!='mov' or not start[0].op_str.startswith('edx, 0x') or start[1].mnemonic!='jmp':
        raise ValueError('Require descriptor immediate followed by direct tail jump')
    try:descriptor=int(start[0].op_str.split(', ')[1],0);route=int(start[1].op_str,0)
    except ValueError:raise ValueError('Not an immediate descriptor/direct target')
    if start[0].size!=5 or start[1].size!=5:raise ValueError('Require complete ten-byte setup')
    rows=decode(dispatcher,number(functions[dispatcher]['end']))
    tail=[(i.mnemonic,i.op_str) for i in rows[-8:]]
    if tail!=[('and','al, 0xf'),('xlatb',''),('movsx','eax, al'),('and','ecx, 0x404'),
              ('mov','ebx, edx'),('add','ebx, eax'),('add','ebx, 0x10'),('jmp','dword ptr [ebx]')]:
        raise ValueError('Unknown selector/indexed-dispatch ABI')
    loads=[i for i in rows[:-8] if i.mnemonic=='mov' and i.op_str.startswith('ebx, 0x')]
    if len(loads)!=1:raise ValueError('Ambiguous classification selector table')
    selector=int(loads[0].op_str.split(', ')[1],0)
    suffix=[i for i in rows[:-8] if i.address>loads[0].address]
    if any(i.mnemonic in ('call','jmp','ret') or (i.op_str.startswith(('ebx,','bx,','bl,','bh,')) and i.mnemonic not in ('cmp','test')) for i in suffix):
        raise ValueError('Selector base can change before XLAT')
    offsets=list(read(selector,16))
    if set(offsets)!={0,4,8,12}:raise ValueError('Unknown descriptor target domain')
    # Follow original indexed control-flow edges only; evidence of a route is
    # not a claim that every path preserves the descriptor or runs at boot.
    pending=deque([(route,[])]);seen=set();path=None
    while pending and len(seen)<128:
        at,history=pending.popleft()
        if at==dispatcher:path=history+[at];break
        if at in seen or at not in functions:continue
        seen.add(at);end=number(functions[at]['end'])
        if not 0<end-at<=4096:continue
        ins=decode(at,end);targets=[]
        for i in ins:
            if i.mnemonic=='call' or i.mnemonic.startswith('j'):
                try:targets.append(int(i.op_str,0))
                except ValueError:pass
        if ins[-1].mnemonic not in ('ret','jmp','int3','ud2','hlt'):targets.append(end)
        pending.extend((target,history+[at]) for target in targets if target in functions or target==dispatcher)
    if path is None:raise ValueError('No indexed direct path to selector dispatcher')
    table=read(descriptor+16,16);targets=list(struct.unpack('<4I',table));entries=[]
    for slot,target in enumerate(targets):
        owners=[];boundary=False
        for at,f in functions.items():
            end=number(f['end'])
            if at<=target<end:
                owners.append(dict(start=at,end=end))
                if end-at<=65536:
                    boundary|=any(i.address==target for i in decode(at,end))
        # Context is explicit evidence only; do not call arbitrary decoded
        # data a new function merely because decoding at the pointer works.
        context=[]
        for i in decoder.disasm(read(target,128),target):
            context.append(dict(address=i.address,size=i.size,mnemonic=i.mnemonic,operands=i.op_str))
            if i.mnemonic=='ret' or len(context)>=32:break
        entries.append(dict(slot=slot,pointer_address=descriptor+16+slot*4,target=target,
                            already_indexed=target in functions,owners=owners,
                            existing_linear_boundary=boundary,context=context))
    recovery=recover_entries(decoder,read,functions,targets)
    return dict(status='AUDIT_ONLY_NO_ENTRIES_REGISTERED',setup=setup,descriptor=descriptor,
                dispatcher=dispatcher,selector=selector,selector_offsets=offsets,
                indexed_route=[setup,*path],table_sha256=hashlib.sha256(table).hexdigest(),
                unaligned_pointer_table=bool((descriptor+16)&3),entries=entries,
                recovered_cfg_entries=recovery,registered_entries=0,engine_started=False,cold_boot_supported=False)


def audit(exe,analysis,setup,dispatcher,deps):
    sys.path.insert(0,str(deps.resolve()))
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    from audit_startup import audit as identity
    from audit_install import pe_info
    from audit_aot_gaps import sections_from_container
    proof=identity(exe,analysis);original=exe.read_bytes();pe=pe_info(exe)
    profile=json.loads((analysis/'input-profile.json').read_text());raw=(analysis/'fnv-analysis.xbe').read_bytes()
    sections=sections_from_container(raw,profile)
    data=(analysis/'disasm/functions.json').read_bytes();functions={number(f['start']):f for f in json.loads(data)}
    def read(at,size):
        s=next((s for s in sections if s['start']<=at and at+size<=s['start']+s['stored']),None)
        if not s or not 0<size<=65536:raise ValueError('Requested evidence is not wholly backed')
        out=raw[s['offset']+at-s['start']:s['offset']+at-s['start']+size]
        if not s['executable']:
            r=next((r for r in pe['sections'] if pe['image_base']+r['rva']<=at and at+size<=pe['image_base']+r['rva']+r['raw_bytes']),None)
            if not r:raise ValueError('Descriptor/selector is not original data')
            off=r['raw_offset']+at-pe['image_base']-r['rva']
            if out!=original[off:off+size]:raise ValueError('Captured table differs from original data')
        return out
    decoder=Cs(CS_ARCH_X86,CS_MODE_32)
    result=inspect_table(decoder,read,functions,setup,dispatcher)
    if (identity(exe,analysis)!=proof or exe.read_bytes()!=original or
        (analysis/'fnv-analysis.xbe').read_bytes()!=raw or (analysis/'disasm/functions.json').read_bytes()!=data):
        raise ValueError('Inputs changed during table audit')
    result.update(identity=proof,database_sha256=hashlib.sha256(data).hexdigest(),
                  source_sha256={str(Path(__file__).resolve()):sha(Path(__file__))},
                  limits=['Indexed route is existence evidence, not all-path register-liveness proof',
                          'Linear instruction boundary is not independent callable ABI proof',
                          'No functions are registered by this audit'])
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','analysis','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    for name in ('setup','dispatcher'):p.add_argument('--'+name,type=lambda v:int(v,0),required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Fresh output required')
    result=audit(a.exe.resolve(),a.analysis.resolve(),a.setup,a.dispatcher,a.python_deps)
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('AUDIT_ONLY',len(result['entries']),'table entries;',sum(not r['already_indexed'] for r in result['entries']),'not indexed')

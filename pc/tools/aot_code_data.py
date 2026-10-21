"""Audit guarded two-level x86 switches and derive a separate analysis selection.

Only orphan gap_prologue seeds confined to proven table bytes and <=15 bytes
of padding are eligible. Preserve original inputs, code, ABI and bodies: do
not replace functions with success stubs. Detailed reports must stay private.
"""
import argparse
from bisect import bisect_left, bisect_right
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import shutil
import sys
from audit_aot_gaps import address, sections_from_container


def sha(data): return hashlib.sha256(data).hexdigest()


def read_va(raw, sections, start, size):
    if size < 0 or not 0 <= start <= start+size <= 0x100000000:
        return None
    for s in sections:
        if s['start'] <= start and start+size <= s['start']+s['stored']:
            offset = s['offset']+start-s['start']
            return raw[offset:offset+size]
    return None


def padding(raw):
    """Only established x86/MSVC alignment NOPs and INT3, not a prologue."""
    if raw is None or len(raw)>15: return False
    return not raw.replace(bytes.fromhex('8bff'), b'').replace(bytes.fromhex('8d4900'), b'').replace(b'\x90',b'').replace(b'\xcc',b'')


def executable_address(sections, value):
    """True only for a file-backed byte in an executable analysis section."""
    return any(section['executable'] and section['start'] <= value < section['start']+section['stored']
               for section in sections)


def control_refs_into(xrefs, start, end, source_start=None, source_end=None):
    """External direct control transfers into a half-open range.

    A sweep can invent branches while decoding a table as x86.  Sources in that
    same range therefore do not prove an entry; callers from outside still do.
    """
    result=[]
    for ref in xrefs:
        target=address(ref['to']); source=address(ref['from'])
        if ref['type'] not in ('call','jump','cond_jump') or not start <= target < end:
            continue
        if source_start is not None and source_end is not None and source_start <= source < source_end:
            continue
        result.append((source,target,ref['type']))
    return sorted(set(result))


def tail_table_trims(raw, sections, functions, xrefs, decoder, jump_refs=None):
    """Find a final, referenced jump table decoded past an actual ``ret``.

    The evidence is deliberately conjunctive: a recognised function must end
    in a real return, its following bytes must be a table selected by an
    indirect four-byte jump in the preceding code, every entry must already be
    an SDK-recorded direct jump target, and no outside control transfer may
    enter the candidate data.  This is a selection correction only; it never
    invents code or callable entries.
    """
    from capstone.x86 import X86_OP_MEM
    if jump_refs is None:
        jump_refs=sorted((address(ref['from']),address(ref['to'])) for ref in xrefs if ref['type']=='jump')
    jump_sources=[source for source,_ in jump_refs]
    trims=[]
    for function in functions:
        start,end=address(function['start']),address(function['end'])
        if not (function.get('has_prologue') or function.get('called_by')) or \
                function.get('detection_method')=='gap_prologue' or not 0 < end-start <= 0x100000:
            continue
        code=read_va(raw,sections,start,end-start)
        # Both the x86 addressing mode and the bytes are required before the
        # comparatively expensive detailed decode.
        if code is None or b'\xff\x24' not in code: continue
        lite=list(decoder.disasm(code,start))
        if not lite or sum(insn.size for insn in lite)!=len(code): continue
        direct_tables=set()
        for insn in lite:
            if insn.mnemonic!='jmp' or len(insn.operands)!=1 or insn.operands[0].type!=X86_OP_MEM:
                continue
            mem=insn.operands[0].mem
            if not mem.segment and not mem.base and mem.index and mem.scale==4 and mem.disp:
                direct_tables.add(mem.disp & 0xffffffff)
        if not direct_tables: continue
        lo,hi=bisect_left(jump_sources,start),bisect_left(jump_sources,end)
        jump_targets={target for _,target in jump_refs[lo:hi]}
        for ret in reversed(lite):
            if ret.mnemonic!='ret': continue
            code_end=ret.address+ret.size
            # The compiler may align a table after the epilogue.  Do not cross
            # an arbitrary instruction: padding is limited to the same bytes
            # accepted by the existing guarded-table audit.
            table_start=None
            for candidate in sorted(direct_tables):
                if code_end <= candidate <= code_end+15 and padding(read_va(raw,sections,code_end,candidate-code_end)):
                    table_start=candidate;break
            if table_start is None or table_start >= end: continue
            table_end=table_start;targets=[]
            boundaries={insn.address for insn in lite if insn.address < code_end}
            while table_end+4 <= end:
                word=read_va(raw,sections,table_end,4)
                if word is None: break
                target=int.from_bytes(word,'little')
                # Internal case blocks are instruction boundaries inside this
                # function.  External case stubs have already been indexed as
                # SDK jump targets.  Either form is independently evidenced.
                local_case=start <= target < ret.address and target in boundaries
                if not executable_address(sections,target) or not (local_case or target in jump_targets): break
                targets.append(target);table_end+=4
            suffix=read_va(raw,sections,table_end,end-table_end)
            if len(targets)<3 or suffix is None or not padding(suffix): continue
            if control_refs_into(xrefs,code_end,end,code_end,end): continue
            trims.append(dict(function_start=start,function_end=end,new_end=table_start,
                return_address=ret.address,table_start=table_start,table_end=table_end,
                table_entries=len(targets),targets=targets,
                table_sha256=sha(read_va(raw,sections,table_start,table_end-table_start)),
                instructions_before_table=sum(insn.address<table_start for insn in lite)))
            break
    return trims


def orphan_pointer_exclusions(raw, sections, functions, xrefs, ref_points=None):
    """Reject unreferenced hotpatch-prefix pointer tables as gap functions.

    A function-looking ``8b ff`` prefix alone is not enough.  We require an
    unreferenced ``gap_prologue`` seed followed immediately by at least eight
    consecutive executable pointers and only compact selector bytes.  This
    identifies table storage while avoiding a replacement body or a guessed
    entry point.
    """
    if ref_points is None:
        ref_points=sorted({address(ref['from']) for ref in xrefs}|{address(ref['to']) for ref in xrefs})
    excluded=[]
    for function in functions:
        start,end=address(function['start']),address(function['end'])
        if function.get('detection_method')!='gap_prologue' or function.get('has_prologue') or \
                function.get('called_by') or function.get('calls_to') or end-start<2+8*4:
            continue
        point=bisect_left(ref_points,start)
        if point<len(ref_points) and ref_points[point]<end: continue
        blob=read_va(raw,sections,start,end-start)
        if blob is None or blob[:2]!=bytes.fromhex('8bff'): continue
        cursor=2;targets=[]
        while cursor+4 <= len(blob):
            target=int.from_bytes(blob[cursor:cursor+4],'little')
            if not executable_address(sections,target): break
            targets.append(target);cursor+=4
        selectors=blob[cursor:]
        if len(targets)<8 or not selectors or len(selectors)>64 or any(value>0x1f for value in selectors):
            continue
        excluded.append(dict(function=function,reason='orphan-hotpatch-prefix-pointer-table',
            pointer_start=start+2,pointer_end=start+cursor,pointer_count=len(targets),targets=targets,
            pointer_sha256=sha(blob[2:cursor]),selector_sha256=sha(selectors)))
    return excluded


def instruction_boundary_in_known_owner(raw, sections, functions, target, decoder,
                                        starts=None, prefix_max=None, ordered=None):
    """Require a pointer target to be a decoded boundary in an indexed body."""
    if ordered is None:
        ordered=sorted((address(f['start']),address(f['end']),f) for f in functions)
        starts=[item[0] for item in ordered]
        prefix_max=[]; maximum=0
        for _,end,_ in ordered:
            maximum=max(maximum,end);prefix_max.append(maximum)
    i=bisect_right(starts,target)-1
    while i>=0 and prefix_max[i]>target:
        function=ordered[i][2]
        start,end=address(function['start']),address(function['end'])
        if start<=target<end:
            if target==start:return True
            distance=target-start
            if distance<=4096:
                code=read_va(raw,sections,start,min(distance+15,end-start))
                if code is not None:
                    for insn in decoder.disasm(code,start):
                        if insn.address==target:return True
                        if insn.address<target<insn.address+insn.size:break
        i-=1
    return False


def repeated_orphan_pointer_groups(blob, start, sections):
    """Parse the repeated 8bff / absolute-code-pointers / selector record."""
    cursor=0;groups=[]
    while cursor+2<=len(blob) and blob[cursor:cursor+2]==bytes.fromhex('8bff'):
        header=start+cursor;cursor+=2;targets=[];pointer_start=start+cursor
        while cursor+4<=len(blob):
            target=int.from_bytes(blob[cursor:cursor+4],'little')
            if not executable_address(sections,target):break
            targets.append(target);cursor+=4
        minimum=8 if not groups else 5
        if len(targets)<minimum:return None
        selector_start=cursor
        if cursor+14>len(blob):return None
        selectors=blob[cursor:cursor+14]
        if any(value>4 for value in selectors):return None
        cursor+=14
        groups.append(dict(header=header,pointer_start=pointer_start,
            pointer_end=start+selector_start,pointer_count=len(targets),targets=targets,
            pointer_sha256=sha(blob[pointer_start-start:selector_start]),
            selector_start=start+selector_start,selector_end=start+cursor,
            selector_sha256=sha(selectors)))
    if len(groups)<2 or not padding(blob[cursor:]):return None
    return groups,cursor


def repeated_orphan_pointer_exclusions(raw, sections, functions, xrefs,
                                       decoder, ref_points=None):
    """Find repeated, unreferenced pointer/selector tables mis-seeded as code.

    This is narrower than ordinary switch recovery: the complete gap seed must
    be a repeated fixed selector record; every pointer must be executable and
    land on a decoded instruction boundary in another indexed code body; the
    region must have no xref of any kind and end only in short alignment
    padding. No bytes or function bodies are rewritten.
    """
    if ref_points is None:
        ref_points=sorted({address(ref['from']) for ref in xrefs}|{address(ref['to']) for ref in xrefs})
    ordered=sorted((address(f['start']),address(f['end']),f) for f in functions)
    starts=[item[0] for item in ordered];prefix_max=[];maximum=0
    for _,end,_ in ordered:
        maximum=max(maximum,end);prefix_max.append(maximum)
    excluded=[]
    for function in functions:
        start,end=address(function['start']),address(function['end'])
        if function.get('detection_method')!='gap_prologue' or function.get('has_prologue') or \
                function.get('called_by') or function.get('calls_to') or end-start<2+8*4+14+2+5*4+14:
            continue
        point=bisect_left(ref_points,start)
        if point<len(ref_points) and ref_points[point]<end:continue
        blob=read_va(raw,sections,start,end-start)
        if blob is None or blob[:2]!=bytes.fromhex('8bff'):continue
        parsed=repeated_orphan_pointer_groups(blob,start,sections)
        if parsed is None:continue
        groups,used=parsed
        if start+used>end:continue
        if any(not instruction_boundary_in_known_owner(raw,sections,functions,target,decoder,
               starts,prefix_max,ordered)
               for group in groups for target in group['targets']):
            continue
        excluded.append(dict(function=function,reason='orphan-repeated-hotpatch-dispatch-tables',
            groups=groups,table_end=start+used,table_sha256=sha(blob[:used]),
            alignment_padding_sha256=sha(blob[used:])))
    return excluded


def switch_pattern(window, boundaries, owner, raw, sections, decoder):
    from capstone.x86 import X86_OP_REG, X86_OP_IMM, X86_OP_MEM
    if len(window) not in (4,5): return None
    first, guard = window[:2]; load, jump = window[-2:]
    if [first.mnemonic,guard.mnemonic,load.mnemonic,jump.mnemonic] != ['cmp','ja','movzx','jmp']:
        return None
    if any(a.address+a.size!=b.address for a,b in zip(window,window[1:])): return None
    f,g,l,j = (i.operands for i in (first,guard,load,jump))
    if len(f)!=2 or f[1].type!=X86_OP_IMM or not 0 <= f[1].imm < 4096 or \
            len(g)!=1 or g[0].type!=X86_OP_IMM or g[0].imm not in boundaries:
        return None
    reg32={'eax','ecx','edx','ebx','esp','ebp','esi','edi'}
    def reg(op): return op.type==X86_OP_REG and op.size==4 and decoder.reg_name(op.reg) in reg32
    def mem_key(op):
        return (op.mem.segment,op.mem.base,op.mem.index,op.mem.scale,op.mem.disp,op.size)
    if len(window)==5:
        move=window[2]; m=move.operands
        if move.mnemonic!='mov' or len(m)!=2 or not reg(m[0]) or \
                f[0].type!=X86_OP_MEM or f[0].size!=4 or m[1].type!=X86_OP_MEM or \
                mem_key(f[0])!=mem_key(m[1]) or f[0].mem.segment or f[0].mem.index or \
                decoder.reg_name(f[0].mem.base) not in ('ebp','esp'):
            return None
        index=m[0].reg
    else:
        if not reg(f[0]): return None
        index=f[0].reg
    if len(l)!=2 or not reg(l[0]) or l[1].type!=X86_OP_MEM or l[1].size!=1 or \
            l[1].mem.segment or l[1].mem.base!=index or l[1].mem.index or \
            len(j)!=1 or j[0].type!=X86_OP_MEM or j[0].size!=4 or j[0].mem.segment or \
            j[0].mem.base or j[0].mem.index!=l[0].reg or j[0].mem.scale!=4:
        return None
    table, selectors = j[0].mem.disp, l[1].mem.disp
    start,end=address(owner['start']),address(owner['end'])
    if not end <= table <= end+15: return None
    if not padding(read_va(raw,sections,end,table-end)): return None
    selected=read_va(raw,sections,selectors,f[1].imm+1)
    if not selected: return None
    slots=max(selected)+1
    if slots<3 or table+slots*4!=selectors: return None
    pointers=read_va(raw,sections,table,slots*4)
    if pointers is None: return None
    targets=[int.from_bytes(pointers[i:i+4],'little') for i in range(0,len(pointers),4)]
    if any(t not in boundaries or not start<=t<end for t in targets): return None
    home=next((s for s in sections if s['start']<=start<s['end']),None)
    if not home or not home['executable'] or selectors+len(selected)>home['start']+home['stored']:
        return None
    return dict(owner_start=start,owner_end=end,guard=guard.address,dispatch=jump.address,
        compare=first.address,guarded_end=jump.address+jump.size,
        table_start=table,table_end=selectors,selector_start=selectors,selector_end=selectors+len(selected),
        selector_count=len(selected),slots=slots,targets=targets,
        table_sha256=sha(pointers),selector_sha256=sha(selected))


def audit_tables(raw, sections, functions, xrefs, decoder):
    starts=sorted(address(f['start']) for f in functions)
    by_start={address(f['start']):f for f in functions}
    if len(starts)!=len(by_start): raise ValueError('Duplicate function entry')
    incoming=defaultdict(list)
    control=[]
    for ref in xrefs:
        source,target=address(ref['from']),address(ref['to'])
        incoming[target].append(source)
        if ref['type'] in ('call','jump','cond_jump'): control.append((target,source))
    control.sort();control_targets=[p[0] for p in control]
    incoming_targets=sorted(incoming)
    tables=[]; exclusions={};rejected=Counter()
    for owner in functions:
        if owner.get('detection_method')!='prologue' or not owner.get('has_prologue'): continue
        start,end=address(owner['start']),address(owner['end'])
        if not 0 < end-start <= 0x100000: continue
        code=read_va(raw,sections,start,end-start)
        if code is None or b'\xff\x24' not in code: continue
        # A complete decode from an existing prologue, never from the table.
        lite=list(decoder.disasm_lite(code,start))
        if not lite or sum(i[1] for i in lite)!=len(code) or lite[-1][2]!='ret': continue
        boundaries={i[0] for i in lite}
        by_address={i[0]:i for i in lite}
        reachable=set(); pending=[start]
        while pending:
            at=pending.pop()
            if at in reachable or at not in by_address: continue
            reachable.add(at);_,size,mnemonic,operands=by_address[at]
            if mnemonic.startswith('ret') or mnemonic in ('int3','hlt','ud2','ljmp','iretd'): continue
            if mnemonic.startswith('j') or mnemonic.startswith('loop'):
                if operands.startswith('0x'):
                    try: pending.append(int(operands,16))
                    except ValueError: pass
                if mnemonic=='jmp': continue
            pending.append(at+size)
        for n,insn in enumerate(lite):
            if insn[2]!='jmp' or insn[1]!=7 or code[insn[0]-start:insn[0]-start+2]!=b'\xff\x24': continue
            found=None
            for count in (5,4):
                if n+1<count: continue
                first=lite[n+1-count][0]
                block=code[first-start:insn[0]+insn[1]-start]
                window=list(decoder.disasm(block,first))
                if len(window)==count:
                    found=switch_pattern(window,boundaries,owner,raw,sections,decoder)
                if found: break
            if not found: continue
            if found['compare'] not in reachable or incoming_targets[
                    bisect_left(incoming_targets,found['compare']+1):bisect_left(incoming_targets,found['guarded_end'])]:
                rejected['unreachable_or_bypassed_guard']+=1;continue
            lo,hi=found['table_start'],found['selector_end']
            if any(found['compare']<t<found['guarded_end'] for t in found['targets']):
                rejected['switch_bypasses_own_guard']+=1;continue
            # No established entry or outside direct branch may land in data.
            occupied=[by_start[a] for a in starts[bisect_left(starts,lo):bisect_left(starts,hi)]]
            external=[(t,s) for t,s in control[bisect_left(control_targets,lo):bisect_left(control_targets,hi)]
                      if not lo<=s<hi]
            if external or any(f.get('detection_method')!='gap_prologue' for f in occupied):
                rejected['conflicting_entry_or_branch']+=1;continue
            tables.append(found)
            for at in starts[bisect_left(starts,end):bisect_left(starts,hi)]:
                f=by_start[at]; stop=address(f['end'])
                if f.get('detection_method')!='gap_prologue' or f.get('called_by') or f.get('calls_to') or incoming[at]:
                    continue
                if not at<stop<=hi+15: continue
                prefix=read_va(raw,sections,at,max(0,lo-at))
                suffix=read_va(raw,sections,max(at,hi),max(0,stop-hi))
                if not padding(prefix) or not padding(suffix): continue
                if any(a!=at and not lo<=a<hi for a in starts[bisect_left(starts,at):bisect_left(starts,stop)]):
                    continue
                # Also protect explicit entries into the prefix/suffix padding.
                refs=[(t,s) for t,s in control[bisect_left(control_targets,at):bisect_left(control_targets,stop)]
                      if not at<=s<stop]
                if refs: continue
                exclusions[at]=dict(function=f,table_dispatch=found['dispatch'],reason='orphan-gap-prologue-in-guarded-table')
    jump_refs=sorted((address(ref['from']),address(ref['to'])) for ref in xrefs if ref['type']=='jump')
    ref_points=sorted({address(ref['from']) for ref in xrefs}|{address(ref['to']) for ref in xrefs})
    pointer_excluded=orphan_pointer_exclusions(raw,sections,functions,xrefs,ref_points)
    pointer_excluded.extend(repeated_orphan_pointer_exclusions(
        raw,sections,functions,xrefs,decoder,ref_points))
    for item in pointer_excluded:
        at=address(item['function']['start'])
        # A strict guarded switch is stronger evidence for the same seed;
        # retain that classification while accepting the independent check.
        if at not in exclusions: exclusions[at]=item
    trims=tail_table_trims(raw,sections,functions,xrefs,decoder,jump_refs)
    excluded=[exclusions[a] for a in sorted(exclusions)]
    excluded_starts={address(item['function']['start']) for item in excluded}
    trim_starts={item['function_start'] for item in trims}
    if excluded_starts & trim_starts:
        raise ValueError('A function cannot be both excluded and trimmed')
    return dict(tables=tables,excluded=excluded,tail_trims=trims,
        rejected_patterns=dict(rejected),functions_before=len(functions),functions_after=len(functions)-len(excluded),
        tail_trimmed_functions=len(trims))


def load_inputs(analysis):
    analysis=analysis.resolve();scan_path=analysis/'scan-identity.json'
    scan=json.loads(scan_path.read_bytes()); profile_bytes=(analysis/'input-profile.json').read_bytes()
    profile=json.loads(profile_bytes); raw=(analysis/'fnv-analysis.xbe').read_bytes()
    if not profile.get('analysis_only') or sha(raw)!=scan['input_sha256'] or sha(raw)!=profile['analysis_container_sha256']:
        raise ValueError('Input identity mismatch')
    files={}
    for name,digest in scan['files'].items():
        if Path(name).name!=name or not name.endswith('.json'): raise ValueError('Unsafe scan filename')
        data=(analysis/'disasm'/name).read_bytes()
        if sha(data)!=digest: raise ValueError('Modified scan file: '+name)
        files[name]=data
    wrapper=(analysis/'aot-index-used.py').read_bytes()
    if sha(wrapper)!=scan['index_wrapper_sha256']: raise ValueError('Missing exact scan wrapper')
    return raw,profile,scan,files,dict(input_profile_sha256=sha(profile_bytes),
        parent_scan_sha256=sha(scan_path.read_bytes()),tool_sha256=sha(Path(__file__).read_bytes()))


def filtered_files(files, functions, excluded, trims=()):
    trims=list(trims)
    trim_by_start={item['function_start']:item for item in trims}
    if len(trim_by_start)!=len(trims):
        raise ValueError('Duplicate tail trim')
    files=dict(files);kept=[]
    for function in functions:
        start=address(function['start'])
        if start in excluded: continue
        if start in trim_by_start:
            trim=trim_by_start[start];updated=dict(function)
            if address(updated['end'])!=trim['function_end'] or not start < trim['new_end'] < trim['function_end']:
                raise ValueError('Invalid tail trim range')
            updated['end']=f'0x{trim["new_end"]:08X}'
            updated['size']=trim['new_end']-start
            updated['num_instructions']=trim['instructions_before_table']
            function=updated
        kept.append(function)
    files['functions.json']=(json.dumps(kept,indent=2)+'\n').encode()
    summary=json.loads(files['summary.json'])
    summary.update(total_functions=len(kept),functions_with_prologue=sum(bool(f.get('has_prologue')) for f in kept),
        functions_by_method=dict(Counter(f['detection_method'] for f in kept)),
        functions_by_section=dict(Counter(f['section'] for f in kept)),
        total_instructions=sum(int(f.get('num_instructions',0)) for f in kept))
    files['summary.json']=(json.dumps(summary,indent=2)+'\n').encode()
    return files


def revalidate_carry_analysis(path, raw, profile, decoder):
    """Reproduce the old strict table proof against its preserved parent scan.

    This is used only when a later index has split the function containing the
    same table.  The earlier report is not trusted by itself: its parent scan,
    table bytes and guarded switch findings are decoded again from the same
    immutable executable image.
    """
    path=Path(path).resolve()
    carry_raw,carry_profile,carry_scan,carry_files,_=load_inputs(path)
    declaration=carry_scan.get('code_data_filter')
    if not declaration or carry_scan.get('tail_table_filter'):
        raise ValueError('Carry-forward source must be a prior table-only selection')
    if carry_raw!=raw or carry_profile.get('analysis_container_sha256')!=profile.get('analysis_container_sha256') or \
            carry_profile.get('executable_sha256')!=profile.get('executable_sha256'):
        raise ValueError('Carry-forward source is not the same original executable')
    parent=Path(declaration['parent_analysis']).resolve()
    if parent==path: raise ValueError('Self-referential carry-forward scan')
    parent_raw,parent_profile,parent_scan,parent_files,parent_identity=load_inputs(parent)
    if parent_scan.get('code_data_filter') or parent_raw!=raw:
        raise ValueError('Carry-forward parent is not an original scan of this executable')
    report_bytes=(path/'code-data-audit.json').read_bytes();report=json.loads(report_bytes)
    if sha(report_bytes)!=declaration['audit_sha256'] or \
            report.get('identity',{}).get('input_profile_sha256')!=parent_identity['input_profile_sha256'] or \
            report.get('identity',{}).get('parent_scan_sha256')!=parent_identity['parent_scan_sha256']:
        raise ValueError('Carry-forward audit identity mismatch')
    old_functions=json.loads(parent_files['functions.json'])
    old_findings=audit_tables(parent_raw,sections_from_container(parent_raw,parent_profile),old_functions,
        json.loads(parent_files['xrefs.json']),decoder)
    strict_old=[item for item in old_findings['excluded']
                if item.get('reason')=='orphan-gap-prologue-in-guarded-table']
    # The newer index may reveal more guarded switches in code the old scan
    # missed. Every earlier finding must still reproduce exactly; additional
    # findings do not invalidate the earlier seeds.
    table_keys={json.dumps(item,sort_keys=True) for item in old_findings['tables']}
    strict_keys={json.dumps(item,sort_keys=True) for item in strict_old}
    prior_table_keys={json.dumps(item,sort_keys=True) for item in report.get('tables',[])}
    prior_excluded_keys={json.dumps(item,sort_keys=True) for item in report.get('excluded',[])}
    if not prior_table_keys or not prior_table_keys<=table_keys or prior_excluded_keys!=strict_keys:
        raise ValueError('Carry-forward table evidence cannot be reproduced from its parent')
    old_excluded=sorted(address(item['function']['start']) for item in strict_old)
    if declaration.get('excluded_entries')!=old_excluded:
        raise ValueError('Carry-forward selection does not match the reproduced table seeds')
    # The earlier selection may have been derived with the pre-tail-trim tool.
    # Verify its retained function inventory directly and preserve all other
    # metadata files byte-for-byte.
    kept=[function for function in old_functions if address(function['start']) not in set(old_excluded)]
    if json.loads(carry_files['functions.json'])!=kept:
        raise ValueError('Carry-forward function list differs from its verified table exclusion')
    for name,data in parent_files.items():
        if name in ('functions.json','summary.json'): continue
        if carry_files.get(name)!=data:
            raise ValueError('Carry-forward changed unrelated scan data: '+name)
    return dict(path=str(path),audit_sha256=sha(report_bytes),parent_scan_sha256=parent_identity['parent_scan_sha256'],
        excluded=strict_old,raw_sha256=sha(raw))


def carry_forward_exclusions(raw, profile, sections, functions, xrefs, carry_analysis, decoder,
                             already_excluded):
    """Carry prior strict table seeds only when current function bounds match."""
    if carry_analysis is None: return [],None
    evidence=revalidate_carry_analysis(carry_analysis,raw,profile,decoder)
    current={address(function['start']):function for function in functions}
    report=json.loads((Path(evidence['path'])/'code-data-audit.json').read_bytes())
    tables={table['dispatch']:table for table in report['tables']}
    carried=[]
    for item in evidence['excluded']:
        start=address(item['function']['start'])
        if start in already_excluded: continue
        function=current.get(start)
        table=tables.get(item['table_dispatch'])
        if function!=item['function'] or table is None:
            raise ValueError('Carry-forward function metadata or table is absent from current index')
        lo,hi=address(function['start']),address(function['end'])
        table_start,table_end=table['table_start'],table['table_end']
        selectors_end=table['selector_end']
        if not table_start-15 <= lo <= table_start or not selectors_end <= hi <= selectors_end+15:
            raise ValueError('Carry-forward seed no longer fits its audited table interval')
        before=read_va(raw,sections,lo,table_start-lo)
        after=read_va(raw,sections,selectors_end,hi-selectors_end)
        table_bytes=read_va(raw,sections,table_start,table_end-table_start)
        selector_bytes=read_va(raw,sections,table['selector_start'],selectors_end-table['selector_start'])
        if not padding(before) or not padding(after) or table_bytes is None or selector_bytes is None or \
                sha(table_bytes)!=table['table_sha256'] or sha(selector_bytes)!=table['selector_sha256']:
            raise ValueError('Carry-forward table bytes or adjacent padding changed')
        if control_refs_into(xrefs,lo,hi,lo,hi):
            raise ValueError('Carry-forward seed gained an external control-flow entry')
        carried.append(dict(function=function,table_dispatch=item['table_dispatch'],
            reason='revalidated-prior-guarded-table-seed',table_sha256=table['table_sha256'],
            selector_sha256=table['selector_sha256'],carry_audit_sha256=evidence['audit_sha256'],
            carry_parent_scan_sha256=evidence['parent_scan_sha256']))
    return carried,evidence


def verify_derivation(analysis, decoder):
    """Reproduce the decision from the preserved parent, not a trusted allowlist."""
    raw,profile,scan,files,_=load_inputs(analysis)
    declared=scan.get('code_data_filter')
    if not declared: return None
    parent=Path(declared['parent_analysis']).resolve()
    if parent==analysis.resolve(): raise ValueError('Self-referential derivation')
    original,_,parent_scan,before,identity=load_inputs(parent)
    if parent_scan.get('code_data_filter'): raise ValueError('Expected an original parent scan')
    report_bytes=(analysis/'code-data-audit.json').read_bytes();report=json.loads(report_bytes)
    if report['identity']!=identity or sha(report_bytes)!=declared['audit_sha256'] or raw!=original or \
            any((parent/n).read_bytes()!=(analysis/n).read_bytes() for n in ('input-profile.json','aot-index-used.py')):
        raise ValueError('Derivation identity mismatch')
    functions=json.loads(before['functions.json'])
    refs=json.loads(before['xrefs.json'])
    findings=audit_tables(original,sections_from_container(original,profile),functions,refs,decoder)
    carry_evidence=None
    carry_path=declared.get('carry_forward_analysis')
    if carry_path:
        carried,carry_evidence=carry_forward_exclusions(original,profile,sections_from_container(original,profile),
            functions,refs,carry_path,decoder,{address(item['function']['start']) for item in findings['excluded']})
        findings['excluded'].extend(carried)
        findings['functions_after']-=len(carried)
        findings['carry_forward']=carry_evidence
    if any(report[k]!=v for k,v in findings.items()): raise ValueError('Code/data evidence cannot be reproduced')
    excluded=sorted(address(e['function']['start']) for e in findings['excluded'])
    trims=findings['tail_trims']
    expected=filtered_files(before,functions,set(excluded),trims)
    expected_filter=dict(identity,audit_sha256=sha(report_bytes),excluded_entries=excluded,
        tail_trims=trims,parent_analysis=str(parent))
    if carry_evidence:
        expected_filter['carry_forward_analysis']=carry_evidence['path']
        expected_filter['carry_forward_audit_sha256']=carry_evidence['audit_sha256']
    expected_scan=dict(parent_scan,files={n:sha(d) for n,d in expected.items()},code_data_filter=expected_filter)
    if files!=expected or scan!=expected_scan: raise ValueError('Filtered selection differs from reproduced evidence')
    return declared


def run(analysis, report_path, deps=None, derive=None, carry_forward_analysis=None):
    if deps: sys.path.insert(0,str(deps.resolve()))
    import capstone
    raw,profile,scan,files,identity=load_inputs(analysis)
    analysis=analysis.resolve();report_path=report_path.resolve()
    if report_path.is_relative_to(analysis) or (derive is not None and derive.resolve().is_relative_to(analysis)):
        raise ValueError('Keep new outputs outside the accepted analysis')
    if derive is not None and (scan.get('code_data_filter') or scan.get('tail_table_filter')): raise ValueError('Derive from an original scan')
    if report_path.exists() or (derive is not None and derive.exists()): raise ValueError('Use fresh private outputs')
    sections=sections_from_container(raw,profile)
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
    functions=json.loads(files['functions.json'])
    refs=json.loads(files['xrefs.json'])
    findings=audit_tables(raw,sections,functions,refs,decoder)
    carry_evidence=None
    if carry_forward_analysis is not None:
        carried,carry_evidence=carry_forward_exclusions(raw,profile,sections,functions,refs,carry_forward_analysis,
            decoder,{address(item['function']['start']) for item in findings['excluded']})
        findings['excluded'].extend(carried)
        findings['functions_after']-=len(carried)
        findings['carry_forward']=carry_evidence
    result=dict(status='CODE_DATA_AUDIT',identity=identity,decoder_version=capstone.__version__,**findings,
        native_engine_started=False,cold_boot_supported=False,
        limits=['Recognizes only a strict guarded two-level MSVC switch pattern',
                'No claim of complete indirect-call reachability or code discovery',
                'Does not change executable bytes or supply replacement functions'])
    if load_inputs(analysis)[-1]!=identity: raise ValueError('Source identity changed during audit')
    if derive is not None:
        if not result['excluded']: raise ValueError('No eligible data seed to exclude')
        derive.mkdir(parents=False)
        (derive/'disasm').mkdir()
        for name in ('input-profile.json','fnv-analysis.xbe','aot-index-used.py'):
            shutil.copyfile(analysis/name,derive/name)
        excluded={address(e['function']['start']) for e in result['excluded']}
        files=filtered_files(files,functions,excluded,result['tail_trims'])
        for name,data in files.items(): (derive/'disasm'/name).write_bytes(data)
        report_bytes=(json.dumps(result,indent=2)+'\n').encode()
        (derive/'code-data-audit.json').write_bytes(report_bytes)
        scan.update(files={n:sha(d) for n,d in files.items()},
            code_data_filter=dict(identity,audit_sha256=sha(report_bytes),excluded_entries=sorted(excluded),
                tail_trims=result['tail_trims'],parent_analysis=str(analysis),
                **({'carry_forward_analysis':carry_evidence['path'],
                    'carry_forward_audit_sha256':carry_evidence['audit_sha256']} if carry_evidence else {})))
        (derive/'scan-identity.json').write_text(json.dumps(scan,indent=2)+'\n')
    report_path.write_text(json.dumps(result,indent=2)+'\n')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--analysis',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--python-deps',type=Path);p.add_argument('--derive',type=Path)
    p.add_argument('--carry-forward-analysis',type=Path,help='Revalidate an earlier strict table selection against the same original executable and current function entries')
    args=p.parse_args();result=run(args.analysis,args.report,args.python_deps,args.derive,args.carry_forward_analysis)
    print(json.dumps({k:v for k,v in result.items() if k not in ('tables','excluded')},indent=2))
    print('tables:',len(result['tables']),'excluded heuristic seeds:',len(result['excluded']))

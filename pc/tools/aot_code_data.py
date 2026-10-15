"""Audit guarded two-level x86 switches and derive a separate analysis selection.

Only orphan gap_prologue seeds confined to proven table bytes and <=15 bytes
of padding are eligible. Preserve original inputs, code, ABI and bodies: do
not replace functions with success stubs. Detailed reports must stay private.
"""
import argparse
from bisect import bisect_left
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
    """Only byte NOP/INT3 and the two-byte MSVC hotpatch NOP, not a prologue."""
    if raw is None or len(raw)>15: return False
    return not raw.replace(bytes.fromhex('8bff'), b'').replace(b'\x90',b'').replace(b'\xcc',b'')


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
    return dict(tables=tables,excluded=[exclusions[a] for a in sorted(exclusions)],
        rejected_patterns=dict(rejected),functions_before=len(functions),functions_after=len(functions)-len(exclusions))


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


def filtered_files(files, functions, excluded):
    files=dict(files);kept=[f for f in functions if address(f['start']) not in excluded]
    files['functions.json']=(json.dumps(kept,indent=2)+'\n').encode()
    summary=json.loads(files['summary.json'])
    summary.update(total_functions=len(kept),functions_with_prologue=sum(bool(f.get('has_prologue')) for f in kept),
        functions_by_method=dict(Counter(f['detection_method'] for f in kept)),
        functions_by_section=dict(Counter(f['section'] for f in kept)))
    files['summary.json']=(json.dumps(summary,indent=2)+'\n').encode()
    return files


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
    findings=audit_tables(original,sections_from_container(original,profile),functions,json.loads(before['xrefs.json']),decoder)
    if any(report[k]!=v for k,v in findings.items()): raise ValueError('Code/data evidence cannot be reproduced')
    excluded=sorted(address(e['function']['start']) for e in findings['excluded'])
    expected=filtered_files(before,functions,set(excluded))
    expected_filter=dict(identity,audit_sha256=sha(report_bytes),excluded_entries=excluded,parent_analysis=str(parent))
    expected_scan=dict(parent_scan,files={n:sha(d) for n,d in expected.items()},code_data_filter=expected_filter)
    if files!=expected or scan!=expected_scan: raise ValueError('Filtered selection differs from reproduced evidence')
    return declared


def run(analysis, report_path, deps=None, derive=None):
    if deps: sys.path.insert(0,str(deps.resolve()))
    import capstone
    raw,profile,scan,files,identity=load_inputs(analysis)
    analysis=analysis.resolve();report_path=report_path.resolve()
    if report_path.is_relative_to(analysis) or (derive is not None and derive.resolve().is_relative_to(analysis)):
        raise ValueError('Keep new outputs outside the accepted analysis')
    if derive is not None and scan.get('code_data_filter'): raise ValueError('Derive from an original scan')
    if report_path.exists() or (derive is not None and derive.exists()): raise ValueError('Use fresh private outputs')
    sections=sections_from_container(raw,profile)
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
    functions=json.loads(files['functions.json'])
    findings=audit_tables(raw,sections,functions,json.loads(files['xrefs.json']),decoder)
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
        files=filtered_files(files,functions,excluded)
        for name,data in files.items(): (derive/'disasm'/name).write_bytes(data)
        report_bytes=(json.dumps(result,indent=2)+'\n').encode()
        (derive/'code-data-audit.json').write_bytes(report_bytes)
        scan.update(files={n:sha(d) for n,d in files.items()},
            code_data_filter=dict(identity,audit_sha256=sha(report_bytes),excluded_entries=sorted(excluded),parent_analysis=str(analysis)))
        (derive/'scan-identity.json').write_text(json.dumps(scan,indent=2)+'\n')
    report_path.write_text(json.dumps(result,indent=2)+'\n')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--analysis',type=Path,required=True);p.add_argument('--report',type=Path,required=True)
    p.add_argument('--python-deps',type=Path);p.add_argument('--derive',type=Path)
    args=p.parse_args();result=run(args.analysis,args.report,args.python_deps,args.derive)
    print(json.dumps({k:v for k,v in result.items() if k not in ('tables','excluded')},indent=2))
    print('tables:',len(result['tables']),'excluded heuristic seeds:',len(result['excluded']))

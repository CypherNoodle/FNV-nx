"""Private static x87 coverage census of an identity-checked analysis selection.

Does not register entries, enable overlays, launch the game, or prove runtime
reachability. Detailed instruction addresses must remain private.
"""
import argparse
from collections import Counter,defaultdict
import hashlib
import json
from pathlib import Path
import sys


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def audit(a):
    if a.output.exists():raise ValueError('Fresh output required')
    analysis=a.analysis.resolve();sdk=a.sdk.resolve();pc=Path(__file__).resolve().parents[1]
    sys.path[:0]=[str(a.python_deps.resolve()),str(sdk),str(pc/'tools')]
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    from tools.recomp.disasm import Disassembler
    from aot_x87_extended import emit
    from run_aot_analysis import sdk_identity,verified_scan_wrapper
    from audit_aot_gaps import address,sections_from_container
    from aot_code_data import read_va,verify_derivation
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True;disassembler=Disassembler()
    lite=Cs(CS_ARCH_X86,CS_MODE_32)
    profile_path=analysis/'input-profile.json';scan_path=analysis/'scan-identity.json'
    profile=json.loads(profile_path.read_text());scan=json.loads(scan_path.read_text());image=analysis/'fnv-analysis.xbe'
    sdk_hash=sdk_identity(sdk)
    if not profile['analysis_only'] or sha(image)!=profile['analysis_container_sha256'] or scan['input_sha256']!=sha(image) or scan['sdk_sha256']!=sdk_hash:
        raise ValueError('Analysis image/SDK identity mismatch')
    wrapper=verified_scan_wrapper(analysis,scan,pc/'tools/aot_index.py')
    for name,digest in scan['files'].items():
        if sha(analysis/'disasm'/name)!=digest:raise ValueError('Changed analysis selection')
    selection=verify_derivation(analysis,decoder) if scan.get('code_data_filter') else None
    function_path=analysis/'disasm/functions.json';functions=json.loads(function_path.read_text());raw=image.read_bytes();sections=sections_from_container(raw,profile)
    paths=[Path(__file__).resolve(),pc/'tools/aot_x87_extended.py',pc/'tools/aot_sahf_flags.py',pc/'tools/audit_aot_gaps.py',pc/'tools/aot_code_data.py',pc/'tools/run_aot_analysis.py',wrapper,profile_path,scan_path,function_path,image]
    frozen={str(p):sha(p) for p in paths}
    a.output.mkdir(parents=True);counts=Counter();unsupported=Counter();details=[];seen={};conflicts=[];failures=[];branches=[];owners=defaultdict(set)
    env={'fnstenv','fldenv','fnsave','frstor'};nonx87_float={'femms'}
    for index,function in enumerate(functions):
        start,end=address(function['start']),address(function['end']);data=read_va(raw,sections,start,end-start)
        if data is None:failures.append(dict(start=start,end=end,reason='unmapped function range'));continue
        sahf=False
        # The SDK's no-resync linear decode uses this same Capstone stream.
        # Only relevant instructions need expensive detailed operand objects.
        for va,size,mnemonic,operands in lite.disasm_lite(data,start):
            if mnemonic=='sahf':sahf=True
            elif sahf and (mnemonic.startswith('j') or mnemonic.startswith('set') or mnemonic.startswith('cmov')):
                branches.append(dict(address=va,mnemonic=mnemonic,function=start));sahf=False
            elif sahf and mnemonic in ('cmp','test','add','sub','and','or','xor','inc','dec','neg','shl','shr','sar','call','ret'):
                sahf=False
            if not (mnemonic.startswith('f') or mnemonic in ('wait','sahf')):continue
            decoded=next(decoder.disasm(data[va-start:va-start+size],va,count=1))
            ins=disassembler._decode_instruction(decoded)
            owners[ins.address].add(start)
            prior=seen.get(ins.address)
            if prior is not None:
                if prior!=ins.bytes_hex:conflicts.append(dict(address=ins.address,function=start))
                continue
            seen[ins.address]=ins.bytes_hex;m=ins.mnemonic;counts[m]+=1
            if m=='sahf':category='sahf_bridge_subset_available'
            elif m in env:
                # Capstone's width is wrong for full-state and 66 env forms.
                encoded=bytes.fromhex(ins.bytes_hex);prefix=[]
                for byte in encoded:
                    if byte in (0x26,0x2e,0x36,0x3e,0x64,0x65,0x66,0x67,0xf0,0xf2,0xf3):prefix.append(byte)
                    else:break
                category='environment32_opt_in' if not any(x in prefix for x in (0x66,0x67,0x64,0x65,0xf0,0xf2,0xf3)) else 'unsupported_environment_addressing'
            elif m in nonx87_float:category='unsupported_non_x87_extension'
            else:
                try:code=emit(ins)
                except (ValueError,KeyError,IndexError,AttributeError) as error:
                    failures.append(dict(address=ins.address,function=start,reason=type(error).__name__));category='emitter_error'
                else:category='supported_staging_form' if code is not None else 'unsupported_staging_form'
            if category not in ('supported_staging_form','environment32_opt_in','sahf_bridge_subset_available'):unsupported[m]+=1
            details.append(dict(address=ins.address,mnemonic=m,operand_widths=[o.mem_size for o in ins.operands if o.type=='mem'],category=category))
        if (index+1)%10000==0:print('audited',index+1,'function ranges',flush=True)
    categories=Counter(d['category'] for d in details)
    for p,digest in frozen.items():
        if sha(Path(p))!=digest:raise ValueError('Audit input changed: '+p)
    assert sdk_identity(sdk)==sdk_hash
    result=dict(status='AUDITED_WITH_GAPS' if unsupported or conflicts or failures else 'AUDITED_STATIC_FORMS',function_ranges=len(functions),
        unique_relevant_instructions=len(details),mnemonics=dict(counts.most_common()),categories=dict(categories),
        unsupported_mnemonics=dict(unsupported.most_common()),sahf_followed_by_condition_sites=len({b['address'] for b in branches}),
        decode_conflicts=conflicts,analysis_failures=failures,source_sha256=frozen,sdk_sha256=sdk_hash,code_data_selection=selection,
        input_code_source=profile.get('code_source'),runtime_reachability_proven=False,engine_generation_migrated=False,
        limits=['Static selected ranges may include overlapping entries and code/data ambiguity','Supported opcode forms do not imply all operand/state inputs are supported','Not a cold EXE startup, build, frame or performance result'])
    (a.output/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    (a.output/'instructions.json').write_text(json.dumps(details,indent=2)+'\n')
    (a.output/'sahf-conditions.json').write_text(json.dumps(branches,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('status','function_ranges','unique_relevant_instructions','categories','unsupported_mnemonics','sahf_followed_by_condition_sites')},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('analysis','sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    audit(p.parse_args())

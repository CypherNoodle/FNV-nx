"""Bounded private startup experiment, never a supported cold-entry profile.

Unselected functions and missing imports are diagnostic traps, never success
stubs. All generated/captured code and reports stay private.
"""
import argparse
from collections import deque
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
from audit_startup import audit
from run_aot_analysis import sdk_identity,sha
from audit_install import pe_info


def object_response(path, objects, executable):
    """Keep large object inventories below Windows' process command-line limit."""
    arguments=[*(Path(p).resolve().as_posix() for p in objects),'-o',Path(executable).resolve().as_posix()]
    if any('"' in p or '\n' in p or '\r' in p for p in arguments):
        raise ValueError('Unsupported response-file path')
    path.write_text('\n'.join('"'+p+'"' for p in arguments)+'\n',encoding='utf-8',newline='\n')
    return '@'+str(path.resolve())


def probe(a):
    if a.output.exists() or not 1<=a.budget<=32768:raise ValueError('Fresh output; budget 1..32768')
    if not 1<=a.jobs<=4 or not 0<=a.split<=512:raise ValueError('Use 1..4 compiler jobs and split 0..512')
    if a.budget>512 and not a.split:raise ValueError('Large experiments require --split to bound compiler units')
    if bool(a.generation_name)!=bool(a.generation_validation):raise ValueError('Generation name and validation must be supplied together')
    if a.all_generated and (not a.generation_name or not a.split):raise ValueError('--all-generated requires a validated generation and split compilation')
    sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve())]
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32,CS_GRP_JUMP,CS_GRP_RET
    from capstone.x86_const import X86_OP_IMM
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    from audit_seh_helpers import verify
    from aot_frame_restore import install
    from aot_simd_bits import install as simd
    from aot_mul_flags import install as multiply
    pc=Path(__file__).resolve().parents[1];analysis=a.analysis.resolve();sdk=a.sdk.resolve();identity=audit(a.exe,analysis)
    helpers=verify(analysis,json.loads(a.helpers.read_text()));prolog,epilog=helpers['helpers']
    database=analysis/'disasm/functions.json';db={int(f['start'],0):dict(f) for f in json.loads(database.read_text())}
    profile=json.loads((analysis/'input-profile.json').read_text());image=(analysis/'fnv-analysis.xbe').read_bytes()
    table_reports=[]
    if a.include_table:
        from audit_initializer_table import inspect
        for path in a.include_table:
            report=json.loads(path.read_text())
            if report!=inspect(a.exe.resolve(),analysis,report['start'],report['end']) or report['unknown_roots']:
                raise ValueError('Initializer table audit changed or has unknown entries')
            table_reports.append(report)
        a.include=list(dict.fromkeys([*a.include,*[at for report in table_reports for at in report['roots']]]))
    text=next(s for s in profile['sections'] if s['name']=='.text');low=profile['image_base']+text['rva'];high=low+text['virtual_bytes']
    count,table=struct.unpack_from('<II',image,0x11c);sections=[struct.unpack_from('<5I',image,table-profile['image_base']+i*56) for i in range(count)]
    row=next(r for r in sections if r[1]==low)
    def body(start,end):
        if not low<=start<end<=high:raise ValueError('Function outside text')
        offset=row[3]+start-low;return image[offset:offset+end-start]
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True
    explicit_wrapper=None
    if a.entry_end is not None:
        if a.entry in db:raise ValueError('Explicit wrapper may not override an existing function')
        code=body(a.entry,a.entry_end);ins=list(decoder.disasm(code,a.entry))
        if len(code)!=10 or len(ins)!=2 or [i.mnemonic for i in ins]!=['call','jmp'] or any(i.size!=5 or i.operands[0].type!=X86_OP_IMM or i.operands[0].imm not in db for i in ins):
            raise ValueError('Require complete direct call + tail jump to existing functions')
        explicit_wrapper=dict(address=a.entry,end=a.entry_end,body_sha256=hashlib.sha256(code).hexdigest(),
                              targets=[i.operands[0].imm for i in ins],cold_entry_verified=False)
        db[a.entry]=dict(start=hex(a.entry),end=a.entry_end,size=10,name=f'sub_{a.entry:08X}',section='.text',called_by=[])
    if any(at not in db for at in [*a.include,*a.stop_at]):raise ValueError('Requested address absent from existing function database')
    if a.entry in a.stop_at:raise ValueError('Cannot exclude the requested entry')
    config.configure_from_xbe(str(analysis/'fnv-analysis.xbe'));simd();multiply();install()
    translator=FunctionTranslator(image,db,seh_prolog=prolog['address'],seh_epilog=epilog['address'])
    generation_info=None;generation_inputs=[];generation_bodies={};generation_refs={};generation_symbols={}
    if a.generation_name:
        from generated_subset import read_generation
        generation_bodies,generation_refs,generation_symbols,generation_inputs,generation_info=read_generation(
            analysis,a.generation_name,a.generation_validation.resolve(),decoder,sdk_identity(sdk))
        if explicit_wrapper:
            source=translator.translate_function(a.entry,db[a.entry])
            if not source or any(m in source for m in ('translation failed',': unhandled */','recomp_unimpl(')):
                raise ValueError('Unsupported explicit wrapper')
            generation_bodies[a.entry]=source;generation_refs[a.entry]=set(explicit_wrapper['targets'])
            generation_symbols[a.entry]=f'sub_{a.entry:08X}'
    selected={};decode_rejected=set();pending=deque([a.entry,prolog['address'],epilog['address'],*a.include])
    budget=len(generation_bodies) if a.all_generated else a.budget
    if a.all_generated:pending.extend(sorted(generation_bodies))
    while pending and len(selected)<budget:
        at=pending.popleft()
        if at in selected or at not in db or at in a.stop_at:continue
        info=db[at];end=int(info['end'],0) if isinstance(info['end'],str) else info['end']
        if generation_info:
            if at not in generation_bodies:raise ValueError('Known selected function absent from complete generation')
            selected[at]=dict(info,end=end,size=end-at,name=f'sub_{at:08X}')
            pending.extend(sorted(generation_refs[at]));continue
        code=body(at,end);instructions=list(decoder.disasm(code,at))
        selected[at]=dict(info,end=end,size=end-at,name=f'sub_{at:08X}')
        if not instructions or sum(i.size for i in instructions)!=len(code):
            decode_rejected.add(at);continue
        for ins in instructions:
            if ins.mnemonic=='call' or ins.group(CS_GRP_JUMP):
                if ins.operands and ins.operands[0].type==X86_OP_IMM:
                    target=ins.operands[0].imm & 0xffffffff
                    if not at<=target<end:pending.append(target)
        if not instructions[-1].group(CS_GRP_RET) and instructions[-1].mnemonic not in ('jmp','int3','ud2','hlt'):pending.appendleft(end)
    if a.entry not in selected:raise ValueError('Entry absent from existing analysis function database')
    if any(set(report['roots'])-set(selected) for report in table_reports):
        raise ValueError('Budget did not include every audited initializer root')
    own=[Path(__file__),pc/'tests/captured_startup_host.c',pc/'tools/aot_frame_restore.py',pc/'tools/aot_simd_bits.py',pc/'tools/aot_mul_flags.py',
        pc/'tools/audit_startup.py',pc/'tools/audit_install.py',pc/'tools/audit_seh_helpers.py',pc/'tools/run_aot_analysis.py',database,a.helpers.resolve()]
    if table_reports:own += [pc/'tools/audit_initializer_table.py',*[p.resolve() for p in a.include_table]]
    if generation_info:own += [pc/'tools/generated_subset.py',pc/'tools/verify_aot_code_data.py',pc/'tools/build_cpu_runtime.py',pc/'tools/aot_code_data.py',*generation_inputs]
    native=[pc/'native'/n for n in ('cpu_runtime.c','pe_image.c','pe_tls.c','windows_thread.c','windows_modules.c','guest_memory.c','memory_horizon.c','windows_memory.c')]
    frozen={str(p.resolve()):sha(p) for p in [*own,*native,Path(a.cc[0]).resolve(),*sorted((pc/'native').glob('*.h')),*sorted((pc/'native').glob('*.inc')),
        *[p for d in (sdk/'templates/runtime',sdk/'src/kernel') for p in d.rglob('*.h')]]}
    sdk_hash=sdk_identity(sdk);a.output.mkdir(parents=True);out=a.output.resolve()
    # Keep all known boundaries visible: an unselected hotpatch continuation
    # must become an explicit trap, never an empty successful C return.
    snippets=[];rejected=[]
    for at,info in selected.items():
        source=generation_bodies[at] if generation_info else '' if at in decode_rejected else translator.translate_function(at,info)
        if at in decode_rejected or '/* translation failed */' in source or ': unhandled */' in source:
            rejected.append(at);source=f'void {generation_symbols.get(at,f"sub_{at:08X}")}(void){{recomp_unimpl("Rejected startup translation",0x{at:X}u);}}\n'
        snippets.append(source.replace('RECOMP_ICALL_SAFE_AT(', 'FNV_STARTUP_ICALL('))
    references=({v for at in selected for v in generation_refs[at]} if generation_info else
                {int(m,16) for source in snippets for m in re.findall(r'\bsub_([0-9A-Fa-f]{8})\b',source)})
    missing=references-set(selected)
    header='#include "fnv_sdk_runtime.h"\n#include "fnv_simd_bits.h"\n#include "fnv_mul_flags.h"\n'
    header+='extern uint32_t fnv_probe_site;\n#define FNV_STARTUP_ICALL(target, saved, site) do { fnv_probe_site=(site); RECOMP_ICALL_SAFE_AT(target,saved,site); } while(0)\n'
    def symbol(at):return generation_symbols.get(at,f'sub_{at:08X}')
    header+=''.join(f'void {symbol(at)}(void);\n' for at in sorted(references|set(selected)))
    stubs=''.join(f'void {symbol(at)}(void){{recomp_unimpl("Unselected startup target",0x{at:X}u);}}\n' for at in sorted(missing))
    lookup='static const struct {uint32_t at;recomp_func_t function;} probe_functions[]={'+''.join(
        f'{{0x{at:X}u,{symbol(at)}}},' for at in sorted(references|set(selected)))+'};\n'
    lookup+='recomp_func_t recomp_lookup(uint32_t at){size_t low=0,high=sizeof(probe_functions)/sizeof(probe_functions[0]);while(low<high){size_t mid=low+(high-low)/2;if(probe_functions[mid].at<at)low=mid+1;else high=mid;}return low<sizeof(probe_functions)/sizeof(probe_functions[0]) && probe_functions[low].at==at?probe_functions[low].function:NULL;}\n'
    generated=[]
    if a.split:
        (out/'generated_functions.h').write_text(header)
        for i in range(0,len(snippets),a.split):
            path=out/f'generated_{i//a.split:04d}.c'
            path.write_text('#include "generated_functions.h"\n'+''.join(snippets[i:i+a.split]));generated.append(path)
        path=out/'generated_lookup.c';path.write_text('#include "generated_functions.h"\n'+stubs+lookup);generated.append(path)
    else:
        path=out/'generated.c';path.write_text(header+''.join(snippets)+stubs+lookup);generated.append(path)
    generated_files={p.name:sha(p) for p in generated}
    if a.split:generated_files['generated_functions.h']=sha(out/'generated_functions.h')
    imports=[]
    for item in pe_info(a.exe)['imports']:
        for symbol in item['symbols']:imports.append(dict(dll=item['dll'],**symbol))
    text_data=None
    if a.captured_text_data:
        captured=image[row[3]:row[3]+row[4]]
        if len(captured)!=text['virtual_bytes'] or hashlib.sha256(captured).hexdigest()!=profile['text_sha256']:
            raise ValueError('Captured text data does not match the audited analysis image')
        path=out/'captured-text-data.bin';path.write_bytes(captured)
        generated_files[path.name]=sha(path)
        text_data=dict(source='audited-captured-text',rva=text['rva'],bytes=len(captured),sha256=sha(path),
                       executable_file_modified=False,cold_entry_verified=False)
    lines=[f'#define FNV_STARTUP_ENTRY 0x{a.entry:X}u',f'#define FNV_STARTUP_CAPTURED_TEXT {int(a.captured_text_data)}',
           f'#define FNV_STARTUP_TEXT_RVA 0x{text["rva"]:X}u',f'#define FNV_STARTUP_TEXT_BYTES 0x{text["virtual_bytes"]:X}u']
    for i,item in enumerate(imports):
        label=item['dll']+'!'+item.get('name','#'+str(item.get('ordinal',0)))
        lines.append(f'static void missing_import_{i}(void){{recomp_unimpl({json.dumps(label)},0);}}')
    lines.append('static fnv_cpu_function missing_imports[]={'+','.join(f'missing_import_{i}' for i in range(len(imports)))+'};')
    (out/'startup_profile.h').write_text('\n'.join(lines)+'\n')
    generated_files['startup_profile.h']=sha(out/'startup_profile.h')
    def command(args,log):
        r=subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=120)
        (out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(log+' failed: '+r.stdout+r.stderr)
        return r.stdout
    flags=['-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-parentheses-equality','-Wno-unused-label','-Wno-unused-variable','-Wno-unused-but-set-variable','-fno-strict-aliasing','-ffp-contract=off']
    includes=['-I'+str(p) for p in (out,pc/'native',sdk/'templates/runtime',sdk/'src/kernel')]
    compiled=[]
    if a.split:
        sources=[*generated,*native,pc/'tests/captured_startup_host.c']
        def compile_one(path):
            obj=out/(path.stem+'.o')
            command([*a.cc,*flags,*includes,'-include',pc/'native/fnv_sdk_runtime.h','-c',path,'-o',obj],path.stem+'.log')
            print('compiled',path.name,flush=True)
            return dict(source=path.name,object=obj.name,object_sha256=sha(obj))
        with ThreadPoolExecutor(max_workers=a.jobs) as pool:compiled=list(pool.map(compile_one,sources))
        response=out/'link.rsp'
        argument=object_response(response,[out/item['object'] for item in compiled],out/'startup-test.exe')
        generated_files[response.name]=sha(response)
        command([*a.cc,argument],'link.log')
    else:
        command([*a.cc,*flags,*includes,'-include',pc/'native/fnv_sdk_runtime.h',*generated,*native,pc/'tests/captured_startup_host.c','-o',out/'startup-test.exe'],'compiler.log')
    host_args=[out/'startup-test.exe',a.exe.resolve(),a.exe.resolve().parent]
    if a.captured_text_data:host_args.append(out/'captured-text-data.bin')
    host=json.loads(command(host_args,'host.log').strip().splitlines()[-1])
    if audit(a.exe,analysis)!=identity or sdk_identity(sdk)!=sdk_hash or verify(analysis,helpers)!=helpers:raise ValueError('Inputs changed')
    for name,h in frozen.items():
        if sha(Path(name))!=h:raise ValueError('Source changed: '+name)
    if any(sha(out/name)!=h for name,h in generated_files.items()):raise ValueError('Generated source changed')
    if any(sha(out/item['object'])!=item['object_sha256'] for item in compiled):raise ValueError('Compiled object changed')
    result=dict(status='EXPERIMENT_STOPPED' if host['faulted'] else 'EXPERIMENT_RETURNED',host=host,
        identity=identity,source_sha256=frozen,sdk_sha256=sdk_hash,selected_functions=len(selected),
        rejected_functions=rejected,unselected_direct_targets=len(missing),budget=a.budget,explicit_wrapper=explicit_wrapper,included_roots=a.include,explicit_stops=a.stop_at,
        generated_sha256=generated_files.get('generated.c'),generated_files_sha256=generated_files,compiled=compiled,
        executable_sha256=sha(out/'startup-test.exe'),
        initializer_tables=[dict(start=t['start'],end=t['end'],unique_roots=t['unique_roots'],table_sha256=t['table_sha256']) for t in table_reports],
        generation=generation_info,
        text_data=text_data,
        all_generated=a.all_generated,effective_budget=budget,
        native_engine_started=False,cold_boot_supported=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k not in ('source_sha256','identity')},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('exe','analysis','sdk','python-deps','helpers','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--entry',type=lambda s:int(s,0),required=True);p.add_argument('--budget',type=int,default=64)
    p.add_argument('--split',type=int,default=0,help='Functions per generated compiler unit (1..512); required above budget512')
    p.add_argument('--jobs',type=int,default=2,help='Concurrent compiler processes, 1..4, for split experiments')
    p.add_argument('--include-table',type=Path,action='append',default=[],help='Revalidate an explicit initializer audit and include every known root; does not establish ABI or reachability')
    p.add_argument('--generation-name',help='Use exact bodies and references from an already audited complete generation instead of the bounded linear decoder')
    p.add_argument('--generation-validation',type=Path,help='Frozen complete code/data comparison proof for --generation-name')
    p.add_argument('--captured-text-data',action='store_true',help='Use audited captured text bytes also for inline data/tables in the private host image; never changes the original EXE or verifies cold boot')
    p.add_argument('--all-generated',action='store_true',help='Compile every known generated function, retaining explicit --stop-at traps; avoids one missing indirect target per subset run')
    p.add_argument('--entry-end',type=lambda s:int(s,0),help='Isolated explicit 10-byte call/tail-jump wrapper audit, not cold-entry proof')
    p.add_argument('--include',type=lambda s:int(s,0),action='append',default=[],help='Prioritize an existing function in the bounded translation; not an entry override')
    p.add_argument('--stop-at',type=lambda s:int(s,0),action='append',default=[],help='Leave an existing function as an explicit diagnostic trap; do not explore its descendants')
    p.add_argument('--cc',nargs='+',required=True);probe(p.parse_args())

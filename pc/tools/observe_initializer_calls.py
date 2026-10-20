"""Instrument one private AOT initializer-call site without changing its inputs.

Rebuilds one unit from a completed host probe, reuses hash-verified other objects,
and logs caller ESI/frame bounds before and after callbacks. Outputs stay private.
"""
import argparse
import ast
import json
from pathlib import Path
import re
import subprocess
import sys
from run_aot_analysis import sdk_identity,sha
from initializer_trace import table_entries,check_trace


def observe(a):
    if a.output.exists():raise ValueError('Use a fresh private directory')
    proof=json.loads(a.validation.read_text());base=a.validation.resolve().parent;pc=Path(__file__).resolve().parents[1]
    if proof['status'] not in ('EXPERIMENT_STOPPED','EXPERIMENT_RETURNED') or not proof['compiled']:raise ValueError('Require a completed split host probe')
    if sdk_identity(a.sdk.resolve())!=proof['sdk_sha256']:raise ValueError('Changed SDK')
    for name,h in proof['source_sha256'].items():
        if sha(Path(name))!=h:raise ValueError('Changed probe dependency: '+name)
    for name,h in proof['generated_files_sha256'].items():
        if sha(base/name)!=h:raise ValueError('Changed private generated input')
    objects=[]
    for item in proof['compiled']:
        p=base/item['object']
        if sha(p)!=item['object_sha256']:raise ValueError('Changed compiled object')
        objects.append(p)
    script=pc/'tools/probe_captured_startup.py';tree=ast.parse(script.read_text())
    flags=[ast.literal_eval(n.value) for n in ast.walk(tree) if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='flags' for t in n.targets)]
    if len(flags)!=1 or not isinstance(flags[0],list):raise ValueError('Cannot recover exact probe flags')
    pattern=re.compile(r'FNV_STARTUP_ICALL\([^;\n]*?,\s*0x0*'+f'{a.site:X}'+r'u\)',re.I)
    candidates=[]
    for name in proof['generated_files_sha256']:
        if not re.fullmatch(r'generated_\d{4}\.c',name):continue
        text=(base/name).read_text();matches=list(pattern.finditer(text))
        if matches:candidates.append((name,text,matches))
    if len(candidates)!=1 or len(candidates[0][2])!=1:raise ValueError('Require one unambiguous compiled call site')
    name,text,matches=candidates[0];match=matches[0]
    changed=text[:match.start()]+match[0].replace('FNV_STARTUP_ICALL','FNV_OBSERVE_INIT',1)+text[match.end():]
    header='#include "generated_functions.h"\n#include <stdio.h>\n'
    header+='#define FNV_OBSERVE_INIT(t,s,site) do { fprintf(stderr,"FNV_INIT_BEFORE %08X %08X %08X %08X %08X %08X %08X\\n",(unsigned)g_esi,(unsigned)MEM32(ebp+8),(unsigned)(t),(unsigned)ebp,(unsigned)g_ebp,(unsigned)g_esp,(unsigned)g_seh_ebp); FNV_STARTUP_ICALL(t,s,site); fprintf(stderr,"FNV_INIT_AFTER %08X %08X %08X %08X %08X %08X\\n",(unsigned)g_esi,(unsigned)MEM32(ebp+8),(unsigned)ebp,(unsigned)g_ebp,(unsigned)g_esp,(unsigned)g_seh_ebp); } while(0)\n'
    changed_units={name:header+changed}
    for at in a.direct_function:
        owners=[]
        for filename in proof['generated_files_sha256']:
            if re.fullmatch(r'generated_\d{4}\.c',filename) and re.search(r'^void sub_'+f'{at:08X}'+r'\(void\)',(base/filename).read_text(),re.M):owners.append(filename)
        if len(owners)!=1:raise ValueError('Require one selected direct-function owner')
        filename=owners[0];changed_units.setdefault(filename,(base/filename).read_text())
    if a.direct_function:
        abi='#include "generated_functions.h"\n#include <stdio.h>\n#undef RECOMP_ABI_CALL\n'
        abi+='#define RECOMP_ABI_CALL(va,fn) do {uint32_t s=g_esi,b=g_ebp,p=g_esp;(fn)();if(g_esi!=s || g_ebp!=b || g_esp<p+4)fprintf(stderr,"FNV_ABI_CHANGE %08X %08X %08X %08X %08X %08X %08X\\n",(unsigned)(va),(unsigned)s,(unsigned)g_esi,(unsigned)b,(unsigned)g_ebp,(unsigned)p,(unsigned)g_esp);}while(0)\n'
        changed_units={filename:abi+contents for filename,contents in changed_units.items()}
    a.output.mkdir(parents=True);out=a.output.resolve()
    def run(cmd,log):
        r=subprocess.run([str(x) for x in cmd],capture_output=True,text=True,timeout=120)
        (out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(log+' failed: '+r.stderr[-3000:])
        return r
    includes=['-I'+str(p) for p in (base,pc/'native',a.sdk.resolve()/'templates/runtime',a.sdk.resolve()/'src/kernel')]
    replacements={}
    for filename,contents in changed_units.items():
        source=out/filename;source.write_text(contents);obj=out/Path(filename).with_suffix('.o').name
        run([*a.cc,*flags[0],*includes,'-include',pc/'native/fnv_sdk_runtime.h','-c',source,'-o',obj],source.stem+'-compiler.log')
        replacements[base/obj.name]=obj
    linked=[replacements.get(p,p) for p in objects]
    rsp=out/'link.rsp';rsp.write_text('\n'.join('"'+p.as_posix()+'"' for p in linked)+'\n')
    exe=out/'observed.exe';run([*a.cc,'@'+str(rsp),'-o',exe],'link.log')
    args=[exe,a.exe.resolve(),a.exe.resolve().parent]
    if proof.get('text_data'):args.append(base/'captured-text-data.bin')
    executed=run(args,'host.log');host=json.loads(executed.stdout.strip().splitlines()[-1])
    before=[list(map(lambda s:int(s,16),m)) for m in re.findall(r'FNV_INIT_BEFORE '+' '.join(['([0-9A-F]+)']*7),executed.stderr)]
    after=[list(map(lambda s:int(s,16),m)) for m in re.findall(r'FNV_INIT_AFTER '+' '.join(['([0-9A-F]+)']*6),executed.stderr)]
    tables=proof.get('initializer_tables',[])
    if not tables:raise ValueError('Initializer observation requires its audited table')
    entries=table_entries(a.exe.resolve(),tables[-1])
    trace=check_trace(tables[-1],entries,before,after)
    changes=[dict(index=i,before=b,after=e) for i,(b,e) in enumerate(zip(before,after)) if
             b[0]!=e[0] or b[1]!=e[1] or b[3]!=e[2] or b[4]!=e[3] or b[5]+4!=e[4] or b[6]!=e[5]]
    for name,h in proof['source_sha256'].items():
        if sha(Path(name))!=h:raise ValueError('Probe dependency changed during observation')
    result=dict(status='OBSERVED',site=a.site,calls_before=len(before),calls_after=len(after),first=before[:2],last=before[-2:],
                caller_state_changes=changes,host=host,baseline_host_identical=host==proof['host'],
                source_sha256={str(p):sha(p) for p in (Path(__file__).resolve(),Path(__file__).with_name('initializer_trace.py').resolve(),a.validation.resolve(),script,Path(a.cc[0]).resolve())},
                original_inputs_modified=False,native_engine_started=False,switch_hardware_tested=False)
    result['initializer_trace']=dict(before=before,after=after,validation=trace)
    stable=('faulted','result','fault_address','fault_kind','text','last_indirect_site','trace_index','call_count','trace','live_heaps','heap_blocks','process_cleanup_result')
    result['baseline_control_flow_identical']=all(host.get(k)==proof['host'].get(k) for k in stable)
    result['cursor_monotonic']=bool(before) and all(a[0]<b[0] and (b[0]-a[0])%4==0 for a,b in zip(before,before[1:]))
    result['abi_changes']=[list(map(lambda s:int(s,16),m)) for m in re.findall(r'FNV_ABI_CHANGE '+' '.join(['([0-9A-F]+)']*7),executed.stderr)]
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('validation','exe','sdk','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--site',type=lambda s:int(s,0),required=True)
    p.add_argument('--direct-function',type=lambda s:int(s,0),action='append',default=[],help='Observe ABI changes in the selected unit owning this function')
    p.add_argument('--cc',nargs='+',required=True);observe(p.parse_args())

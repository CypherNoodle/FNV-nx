"""Restore one deliberate CRT stop from its exact complete-generation body.

This produces a separate bounded desktop diagnostic. No cold-entry claim,
generated-code edits, guessed function, instruction stub or Switch packaging.
All generated bodies, objects and observations stay private.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from run_aot_analysis import sha,sdk_identity
from x87_target_evaluator import unchanged


def remove_stop(text,symbol,address):
    stub=f'void {symbol}(void){{recomp_unimpl("Unselected startup target",0x{address:X}u);}}'
    if text.count(stub)!=1:raise ValueError('Require exactly one original deliberate stop stub')
    return text.replace(stub,'')


def validate_restored_body(source):
    if not source or any(v in source for v in ('translation failed',': unhandled */')):
        raise ValueError('The next entry has an incomplete translation')
    # Keep exact original operand/state guards and runtime instruction traps.
    # Their presence is not an incomplete translation or a successful stub.


def advance(a):
    if a.output.exists():raise ValueError('Fresh output required')
    baseline=a.baseline.resolve();base=baseline.parent;old=json.loads(baseline.read_text())
    if old.get('status')!='EXPERIMENT_STOPPED' or not old.get('all_generated') or old.get('explicit_stops')!=[a.target]:
        raise ValueError('Require exactly one explicit deliberate stop in complete baseline')
    if sha(a.exe)!=old['identity']['executable_sha256'] or sdk_identity(a.sdk)!=old['sdk_sha256']:
        raise ValueError('Different EXE/SDK')
    frozen={**old['source_sha256'],str(baseline):sha(baseline),str(Path(__file__).resolve()):sha(Path(__file__))}
    for name,h in old['generated_files_sha256'].items():
        if Path(name).name!=name:raise ValueError('Invalid diagnostic filename')
        frozen[str(base/name)]=h
    for item in old['compiled']:frozen[str(base/item['object'])]=item['object_sha256']
    frozen[str(base/'startup-test.exe')]=old['executable_sha256'];unchanged(frozen)
    sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve())]
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    from generated_subset import read_generation
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True
    bodies,refs,symbols,inputs,info=read_generation(a.analysis.resolve(),a.generation_name,a.generation_validation.resolve(),decoder,old['sdk_sha256'])
    if (info!=old['generation'] or a.target not in bodies or a.target in old.get('extra_entries',[])):
        raise ValueError('Target does not belong to matching complete generation')
    for p in inputs:frozen[str(p.resolve())]=sha(p)
    text=(base/'generated_lookup.c').read_text();known={int(v,16) for v in re.findall(r'\{0x([0-9A-Fa-f]+)u,\w+\},',text)}
    if refs[a.target]-known:raise ValueError('Restored entry references unknown diagnostic targets')
    source=bodies[a.target]
    validate_restored_body(source)
    lookup_text=remove_stop(text,symbols[a.target],a.target)
    lines=(base/'link.rsp').read_text().splitlines()
    if any(not (r.startswith('"') and r.endswith('"')) for r in lines):raise ValueError('Unknown original link response syntax')
    args=[r[1:-1] for r in lines]
    if len(args)<3 or args[-2]!='-o' or Path(args[-1])!=base/'startup-test.exe':raise ValueError('Unexpected original output')
    objects=[Path(p) for p in args[:-2]];replace=base/'generated_lookup.o'
    if objects.count(replace)!=1 or any(str(p) not in frozen for p in objects):raise ValueError('Unfrozen or ambiguous original object inventory')
    core=next((Path(p) for p,h in old['source_sha256'].items() if h==old['host_x87']['core_validation_sha256']),None)
    if core is None:raise ValueError('Missing strict core compiler identity')
    cc=json.loads(core.read_text())['compiler'];pc=Path(__file__).resolve().parents[1]
    # The original full diagnostic header owns every preexisting prototype.
    headers=[Path(p) for p in frozen if Path(p).name=='generated_functions.h']
    if len(set(headers))!=1:raise ValueError('Ambiguous original generated header')
    out=a.output.resolve();out.mkdir(parents=True)
    entry=out/'restored_boundary.c';entry.write_text('#include "generated_functions.h"\n'+source.replace('RECOMP_ICALL_SAFE_AT(', 'FNV_STARTUP_ICALL('))
    lookup=out/'generated_lookup.c';lookup.write_text(lookup_text)
    includes=['-I'+str(p) for p in (headers[0].parent,pc/'native',a.sdk/'templates/runtime',a.sdk/'src/kernel')]
    def run(command,name):
        r=subprocess.run([str(v) for v in command],capture_output=True,text=True,timeout=120)
        (out/name).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+': '+r.stderr[-4000:])
        return r.stdout
    compiled=[]
    for p in (entry,lookup):
        obj=p.with_suffix('.o');run([*cc,*old['compiler_flags'],*includes,'-include',pc/'native/fnv_sdk_runtime.h','-c',p,'-o',obj],p.stem+'.log')
        compiled.append(dict(source=p.name,object=obj.name,object_sha256=sha(obj)))
    from probe_captured_startup import object_response
    rsp=out/'link.rsp';program=out/'startup-test.exe'
    reused=[p for p in objects if p!=replace]
    run([*cc,object_response(rsp,[*reused,*[out/i['object'] for i in compiled]],program)],'link.log')
    blob=out/'captured-text-data.bin';shutil.copyfile(base/'captured-text-data.bin',blob)
    from startup_memory_profile import configuration,validate_observation
    memory=configuration(old['memory_profile']);capacity=old['host'].get('worker_stats',{}).get('capacity',0)
    host_args=[program,a.exe.resolve(),a.exe.resolve().parent,blob,*[str(memory[k]) for k in ('arena_bytes','vm_bytes','commit_limit')],'',str(capacity)]
    host=json.loads(run(host_args,'host.log').strip().splitlines()[-1]);validate_observation(host,old['memory_profile'])
    unchanged(frozen)
    result={**old,'status':'EXPERIMENT_STOPPED' if host['faulted'] else 'EXPERIMENT_RETURNED','host':host,
            'source_sha256':frozen,'compiled':compiled,'executable_sha256':sha(program),
            'generated_files_sha256':{p.name:sha(p) for p in (entry,lookup,rsp,blob)},
            'explicit_stops':[],'restored_complete_generation_entry':a.target,
            'restored_body_sha256':__import__('hashlib').sha256(source.encode()).hexdigest(),
            'original_runtime_guards_preserved':True,
            'suitable_for_switch_packaging':False,'reused_compiler_units':len(reused)}
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print('BOUNDARY_RESTORED',hex(a.target),'baseline stop',host['text'],'calls',host['call_count'])
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('baseline','exe','sdk','analysis','python-deps','generation-validation','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--generation-name',required=True);p.add_argument('--target',type=lambda v:int(v,0),required=True)
    advance(p.parse_args())

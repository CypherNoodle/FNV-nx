"""Connect sandboxed profile reads to a frozen private startup diagnostic.

Only the owning Windows unit and INI catalog adapter are rebuilt. Old ABI
headers, game bodies/objects and the arithmetic core must stay byte-identical.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
from x87_target_evaluator import sha,unchanged


def permitted_changes(frozen,pc):
    expected={(pc/n).resolve() for n in ('native/windows_thread.c','tests/captured_startup_host.c')}
    changed={Path(p).resolve() for p,h in frozen.items() if sha(Path(p))!=h}
    if changed!=expected:raise ValueError('Only the INI catalog adapter and owning native unit may change')
    owners={p.resolve() for folder in (pc/'native',pc/'tests',pc/'switch') for p in folder.iterdir()
            if p.suffix in ('.c','.h','.inc') and re.search(r'^\s*#\s*include\s*"windows_profiles.h"',p.read_text(),re.M)}
    expected={(pc/n).resolve() for n in ('native/windows_thread.c','tests/captured_startup_host.c','tests/windows_profiles_host.c','switch/crt_probe.c')}
    if owners!=expected:raise ValueError('Unexpected profile export consumer')
    consumers={p.resolve() for p in (pc/'native').iterdir() if p.suffix in ('.c','.h','.inc') and
               re.search(r'^\s*#\s*include\s*"windows_profiles.inc"',p.read_text(),re.M)}
    if consumers!={(pc/'native/windows_thread.c').resolve()}:raise ValueError('Unexpected INI implementation consumer')
    return changed


def refresh(a):
    if a.output.exists():raise ValueError('Fresh output required')
    baseline=a.baseline.resolve();base=baseline.parent;old=json.loads(baseline.read_text())
    if old.get('status')!='EXPERIMENT_STOPPED' or not old.get('all_generated') or old.get('rejected_functions'):
        raise ValueError('Require a complete stopped host diagnostic')
    if sha(a.exe)!=old['identity']['executable_sha256']:raise ValueError('EXE changed')
    pc=Path(__file__).resolve().parents[1];native=pc/'native'
    frozen=dict(old['source_sha256']);changed=permitted_changes(frozen,pc)
    previous={str(p):frozen[str(p)] for p in changed}
    for p in changed:frozen[str(p)]=sha(p)
    for name,h in old['generated_files_sha256'].items():
        if Path(name).name!=name:raise ValueError('Unsafe generated filename')
        frozen[str(base/name)]=h
    for item in old['compiled']:frozen[str(base/item['object'])]=item['object_sha256']
    for item in old['host_x87_objects']:frozen[item['object']]=item['sha256']
    frozen[str(baseline)]=sha(baseline)
    frozen[str(base/'startup-test.exe')]=old['executable_sha256']
    frozen[str(Path(__file__).resolve())]=sha(Path(__file__))
    provider=[native/'windows_profiles.inc',native/'windows_profiles.h']
    for path in provider:frozen[str(path.resolve())]=sha(path)
    for proof in a.test:
        test=json.loads(proof.read_text())
        if test.get('status')!='PASS':raise ValueError('Failed INI profile regression')
        for p in provider:
            if test['source_sha256'].get(str(p.resolve()))!=sha(p):raise ValueError('Test did not cover current INI profile provider')
        frozen[str(proof.resolve())]=sha(proof)
    unchanged(frozen)
    rows=(base/'link.rsp').read_text().splitlines()
    if any(not (r.startswith('"') and r.endswith('"')) for r in rows):raise ValueError('Unexpected link response')
    args=[r[1:-1] for r in rows]
    if args[-2:]!=['-o',(base/'startup-test.exe').as_posix()]:raise ValueError('Unexpected link output')
    objects=[Path(p) for p in args[:-2]]
    if any(str(p) not in frozen for p in objects):raise ValueError('Unfrozen linked object')
    replace=[p for p in objects if p.name in ('captured_startup_host.o','windows_thread.o')]
    if len(replace)!=2 or len({p.name for p in replace})!=2:raise ValueError('Require exactly one adapter and one Windows native object')
    # Shared ABI headers and every game object remain byte-identical. Rebuild
    # the Windows unit exposing the new bounds accessor and catalog adapter.
    core=next((Path(p) for p,h in frozen.items() if h==old['host_x87']['core_validation_sha256']),None)
    if core is None:raise ValueError('Missing compiler proof')
    cc=json.loads(core.read_text())['compiler']
    out=a.output.resolve();out.mkdir(parents=True)
    def run(command,log):
        r=subprocess.run([str(p) for p in command],capture_output=True,text=True,timeout=120)
        (out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(log+': '+r.stderr[-4000:])
        return r.stdout
    header=[Path(p) for p in frozen if Path(p).name=='generated_functions.h']
    if len(header)!=1:raise ValueError('Ambiguous generated header')
    # SDK roots come from the unchanged original header inventory.
    sdk=a.sdk.resolve()
    from run_aot_analysis import sdk_identity
    if sdk_identity(sdk)!=old['sdk_sha256']:raise ValueError('SDK changed')
    includes=['-I'+str(p) for p in (header[0].parent,native,sdk/'templates/runtime',sdk/'src/kernel')]
    compiled=[]
    for source in (pc/'tests/captured_startup_host.c',native/'windows_thread.c'):
        obj=out/(source.stem+'.o')
        run([*cc,*old['compiler_flags'],*includes,'-include',native/'fnv_sdk_runtime.h','-c',source,'-o',obj],source.stem+'.log')
        compiled.append(dict(source=source.name,object=obj.name,object_sha256=sha(obj)))
    from probe_captured_startup import object_response
    rsp=out/'link.rsp';program=out/'startup-test.exe'
    run([*cc,object_response(rsp,[*[p for p in objects if p not in replace],*[out/i['object'] for i in compiled]],program)],'link.log')
    blob=out/'captured-text-data.bin';shutil.copyfile(base/'captured-text-data.bin',blob)
    from startup_memory_profile import configuration,validate_observation
    memory=configuration(old['memory_profile'])
    host_args=[program,a.exe.resolve(),a.exe.resolve().parent,blob,*[str(memory[k]) for k in ('arena_bytes','vm_bytes','commit_limit')],'',str(old['host']['worker_stats']['capacity'])]
    host=json.loads(run(host_args,'host.log').strip().splitlines()[-1]);validate_observation(host,old['memory_profile'])
    unchanged(frozen)
    result={**old,'status':'EXPERIMENT_STOPPED' if host['faulted'] else 'EXPERIMENT_RETURNED','host':host,
            'source_sha256':frozen,'compiled':compiled,
            'executable_sha256':sha(program),'generated_files_sha256':{p.name:sha(p) for p in (rsp,blob)},
            'new_native_provider_sources':{str(p):sha(p) for p in provider},
            'updated_adapter_sources':dict(previous=previous,current={str(p):sha(p) for p in changed}),
            'reused_link_objects':len(objects)-2,'suitable_for_switch_packaging':False,
            'native_engine_started':False,'cold_boot_supported':False,'switch_hardware_tested':False}
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(status=result['status'],calls=host['call_count'],stop=host['text'],reused=len(objects)-2)))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('baseline','exe','sdk','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--test',type=Path,action='append',required=True)
    refresh(p.parse_args())

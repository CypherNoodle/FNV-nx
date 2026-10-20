"""Real strict host core matching and explicit CPU faults for target-only math."""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys
from pe_fixture import write_pe


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    sys.path.insert(0,str(pc/'tools'))
    from startup_host_x87 import inputs,validate_policy
    from run_aot_analysis import sha
    core=json.loads(a.core.read_text());cc=core['compiler'];frozen={};negatives=0;models=[]
    def reject(call):
        nonlocal negatives
        try:call()
        except ValueError:negatives+=1;return
        raise AssertionError('Invalid host diagnostic configuration accepted')
    for name in ('strict','quad','trig'):
        path=a.fixture/'analysis'/('generation-report-'+name+'-contract.json');report=json.loads(path.read_text())
        flags,deps,objects,policy=inputs(report,a.core,cc,name!='strict');frozen.update(deps);frozen[path.resolve()]=sha(path)
        if len(objects)!=99 or '-DFNV_CPU_EXTENDED_X87=1' not in flags:raise AssertionError('Extended host ABI missing')
        if policy['target_numerical_model_executed'] is not False:raise AssertionError('Host math model overclaimed')
        desktop=dict(host_x87=policy,host_x87_objects=[dict(object=str(p),sha256=sha(p)) for p in objects],
                     compiler_flags=flags,source_sha256={str(p):h for p,h in deps.items()})
        validate_policy(desktop,report)
        for key in ('host_x87','host_x87_objects','compiler_flags','source_sha256'):
            bad_desktop=copy.deepcopy(desktop);bad_desktop.pop(key)
            reject(lambda:validate_policy(bad_desktop,report))
        bad_desktop=copy.deepcopy(desktop);bad_desktop['host_x87']['target_numerical_model_executed']=True
        reject(lambda:validate_policy(bad_desktop,report))
        reject(lambda:inputs(report,None,cc,name!='strict'))
        reject(lambda:inputs(report,a.core,[str(out/'wrong-cc')],name!='strict'))
        reject(lambda:inputs(report,a.core,cc,name=='strict'))
        bad=copy.deepcopy(report);bad['x87_extended_overlay']['legacy_runtime_references']={'bad.c':1}
        reject(lambda:inputs(bad,a.core,cc,name!='strict'))
        models.append(dict(profile=name,host_objects=len(objects),target_math_behavior=policy['target_math_behavior']))
    if inputs({},None,cc,False)!=([],{},[],None):raise AssertionError('Legacy host behavior changed')
    validate_policy({},{});reject(lambda:validate_policy(desktop,{}))
    reject(lambda:inputs({},a.core,cc,False));reject(lambda:inputs({},None,cc,True))
    trig=json.loads((a.fixture/'analysis/generation-report-trig-contract.json').read_text())
    for i,change in enumerate(('status','switch','math','object_count','duplicate','object_digest')):
        bad=copy.deepcopy(core)
        if change=='status':bad['status']='PASS'
        if change=='switch':bad['switch_abi']=True
        if change=='math':bad['transcendental_model']='host-double'
        if change=='object_count':bad['objects']=bad['objects'][:-1]
        if change=='duplicate':bad['objects'][-1]=bad['objects'][0]
        if change=='object_digest':bad['objects'][0]['sha256']='0'*64
        p=out/('bad-core-'+str(i)+'.json');p.write_text(json.dumps(bad))
        reject(lambda:inputs(trig,p,cc,True))
    flags,deps,objects,policy=inputs(trig,a.core,cc,True);frozen.update(deps)
    exe=out/'fixture.exe';write_pe(exe)
    includes=['-I'+str(pc/'native'),'-I'+str(a.sdk.resolve()/'templates/runtime')]
    own=[pc/'native/cpu_runtime.c',pc/'native/pe_image.c',pc/'tests/startup_math_stop_host.c',pc/'tests/startup_math_stop_fault_host.c']
    compiled=[]
    def run(cmd,log):
        r=subprocess.run([str(v) for v in cmd],capture_output=True,text=True)
        (out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr[-4000:])
        return r.stdout
    for source in own:
        frozen[source]=sha(source);obj=out/(source.stem+'.o')
        run([*cc,'-std=c11','-O1','-Wall','-Wextra','-Werror',*flags,*includes,'-include',pc/'native/fnv_sdk_runtime.h','-c',source,'-o',obj],source.stem+'.log')
        compiled.append(obj)
    rsp=out/'link.rsp';rsp.write_text('\n'.join('"'+p.as_posix()+'"' for p in [*compiled,*objects])+'\n')
    program=out/'test.exe';run([*cc,'@'+str(rsp),'-o',program],'link.log')
    result=json.loads(run([program,exe],'host.log').strip().splitlines()[-1])
    if result['status']!='PASS':raise AssertionError('Target math did not fault safely')
    frozen[Path(__file__).resolve()]=sha(Path(__file__).resolve())
    if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Test dependencies changed')
    result.update(models=models,negative_configuration_checks=negatives,
                  source_sha256={str(p):h for p,h in frozen.items()},game_executed=False,hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('core','fixture','sdk','output'):p.add_argument('--'+name,type=Path,required=True)
    test(p.parse_args())

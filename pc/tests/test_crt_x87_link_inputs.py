"""Exercise the CRT packager's x87 inputs with verified synthetic ARM64 objects.

Does not bypass the separate complete-CPU/initializer gates or build an NRO.
"""
import argparse
import copy
import json
from pathlib import Path
import subprocess
import sys


def test(a):
    if a.output.exists():raise ValueError('Use a fresh output directory')
    out=a.output.resolve();out.mkdir(parents=True)
    pc=Path(__file__).resolve().parents[1];sys.path.insert(0,str(pc/'tools'))
    from build_switch_crt_probe import crt_x87_inputs
    from build_cpu_runtime import SWITCH_FLAGS
    from run_aot_analysis import sha
    fixture=a.fixture.resolve();frozen={};negatives=0;models=[]
    def pin(p):
        p=p.resolve();frozen[p]=sha(p);return p
    def reject(call):
        nonlocal negatives
        try:call()
        except ValueError:negatives+=1;return
        raise AssertionError('Incompatible CRT link inputs accepted')
    for name,core_path in [('strict',a.strict_core),('quad',a.quad_core),('trig',a.trig_core)]:
        report_path=pin(fixture/'analysis'/('generation-report-'+name+'-contract.json'))
        report=json.loads(report_path.read_text());core_path=pin(core_path)
        core=json.loads(core_path.read_text());compiler=Path(core['compiler'][0])
        profile=pin(fixture/(name+'.json'))
        # This narrow metadata fixture tests only the ABI/object selector;
        # it is deliberately not a fabricated full CPU-build acceptance proof.
        cpu=dict(compiler_flags=[*SWITCH_FLAGS,'-DFNV_CPU_EXTENDED_X87=1',
                                *[f for f in core['flags'][len(SWITCH_FLAGS):]
                                  if f!='-DFNV_CPU_EXTENDED_X87=1']],
                 x87_core_validation_sha256=sha(core_path))
        from build_cpu_runtime import x87_build_inputs
        cpu['compiler_flags']=x87_build_inputs(report,profile,core_path,compiler)[0]
        deps,objects,digest=crt_x87_inputs(cpu,report,profile,core_path,compiler)
        if len(objects)!=len(core['objects']) or digest!=sha(core_path):raise AssertionError('Dropped core object')
        frozen.update(deps)
        reject(lambda:crt_x87_inputs(cpu,report,None,core_path,compiler))
        reject(lambda:crt_x87_inputs(cpu,report,profile,None,compiler))
        bad=copy.deepcopy(cpu);bad['compiler_flags']=SWITCH_FLAGS
        reject(lambda:crt_x87_inputs(bad,report,profile,core_path,compiler))
        bad=copy.deepcopy(cpu);bad['x87_core_validation_sha256']='0'*64
        reject(lambda:crt_x87_inputs(bad,report,profile,core_path,compiler))
        reject(lambda:crt_x87_inputs(cpu,report,profile,core_path,out/'wrong-gcc'))
        bad_report=copy.deepcopy(report);bad_report['x87_extended_overlay']['legacy_runtime_references']={'bad.c':1}
        reject(lambda:crt_x87_inputs(cpu,bad_report,profile,core_path,compiler))
        bad_core=copy.deepcopy(core);bad_core['objects'][0]['sha256']='0'*64
        bad_core_path=out/(name+'-bad-core.json');bad_core_path.write_text(json.dumps(bad_core))
        bad_cpu=copy.deepcopy(cpu);bad_cpu['x87_core_validation_sha256']=sha(bad_core_path)
        reject(lambda:crt_x87_inputs(bad_cpu,report,profile,bad_core_path,compiler))
        models.append(dict(model=name,objects=len(objects)))
        if name!='trig':continue
        # Link the same actual generated Switch unit with the selector's object
        # list. Omitting that list reproduces unresolved backend references.
        unit=pin(fixture/'trig.o');linker=compiler.with_name('aarch64-none-elf-ld.exe')
        nm=compiler.with_name('aarch64-none-elf-nm.exe');pin(linker);pin(nm)
        def linked_symbols(label,inputs):
            partial=out/(label+'.o');rsp=out/(label+'.rsp')
            args=['-r','-o',partial.as_posix(),*[p.as_posix() for p in inputs]]
            if any('"' in s or '\n' in s or '\r' in s for s in args):raise ValueError('Unsafe response path')
            rsp.write_text('\n'.join('"'+s+'"' for s in args)+'\n')
            r=subprocess.run([str(linker),'@'+str(rsp)],capture_output=True,text=True)
            (out/(label+'-link.log')).write_text(r.stdout+r.stderr)
            if r.returncode:raise RuntimeError(r.stderr)
            r=subprocess.run([str(nm),'-u',str(partial)],capture_output=True,text=True)
            (out/(label+'-symbols.log')).write_text(r.stdout+r.stderr)
            if r.returncode:raise RuntimeError(r.stderr)
            return {line.split()[-1] for line in r.stdout.splitlines() if line.strip()},sha(partial)
        without,_=linked_symbols('without-core',[unit])
        with_core,linked_digest=linked_symbols('with-core',[unit,*objects])
        required={'fnv_x87_trig_candidate','fnv_x87_transcendental_candidate'}
        if not required<=without or required.intersection(with_core):raise AssertionError('Missing-core regression not detected')
    legacy=dict(compiler_flags=list(SWITCH_FLAGS))
    deps,objects,digest=crt_x87_inputs(legacy,{},None,None,compiler)
    if objects or digest is not None:raise AssertionError('Implicit legacy core')
    reject(lambda:crt_x87_inputs(legacy,{},profile,core_path,compiler))
    bad=dict(legacy,x87_core_validation_sha256='0'*64)
    reject(lambda:crt_x87_inputs(bad,{},None,None,compiler))
    for p in (Path(__file__),pc/'tools/build_switch_crt_probe.py',pc/'tools/build_cpu_runtime.py'):pin(p)
    if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Test inputs changed')
    result=dict(status='PASS',models=models,negative_checks=negatives,
                missing_core_control_reproduced=True,linked_core_backend_symbols_resolved=True,
                partial_sha256=linked_digest,source_sha256={str(p):h for p,h in frozen.items()},
                complete_cpu_acceptance_tested=False,nro_built=False,engine_started=False,hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('fixture','strict-core','quad-core','trig-core','output'):p.add_argument('--'+name,type=Path,required=True)
    test(p.parse_args())

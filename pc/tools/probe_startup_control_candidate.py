"""Relink an existing private CRT diagnostic with an isolated FLDCW fix.

This experiment cannot provide production packaging evidence. It replaces
one own core object, retains every original guest object and deliberate stop,
and records the replacement explicitly. It never modifies the installation.
"""
import argparse
import json
from pathlib import Path
import subprocess
from run_aot_analysis import sha
from probe_captured_startup import object_response


def probe(a):
    if a.output.exists():raise ValueError('Fresh private output required')
    out=a.output.resolve();out.mkdir(parents=True)
    baseline=json.loads(a.baseline.read_text());core=json.loads(a.host_core.read_text())
    candidate=json.loads(a.candidate_proof.read_text());base=a.baseline.resolve().parent
    if (baseline.get('status')!='EXPERIMENT_STOPPED' or not baseline.get('all_generated') or
        baseline.get('native_engine_started') is not False or
        baseline['host'].get('fault_address')!=0xee5cbe or
        baseline['host'].get('text')!='extended x87 operand/state unsupported'):
        raise ValueError('Require the complete diagnostic stopped at the audited FLDCW')
    if (candidate.get('status')!='CANDIDATE_PASS' or candidate.get('native_control_words')!=65536 or
        candidate.get('arm64_control_words')!=65536 or candidate.get('atomic_negative_cases')!=20):
        raise ValueError('Require exhaustive native/ARM64 control-word verification')
    policy=baseline['host_x87']
    if (core.get('status')!='COMPILED' or core.get('switch_abi') is not False or
        core.get('transcendental_model') is not None or len(core['objects'])!=99 or
        policy.get('core_validation_sha256')!=sha(a.host_core)):
        raise ValueError('Require the original strict native arithmetic core')
    frozen={Path(p).resolve():h for p,h in baseline['source_sha256'].items()}
    for report in (candidate,core):
        for name,h in report['source_sha256'].items():
            p=Path(name).resolve()
            if p in frozen and frozen[p]!=h:raise ValueError('Conflicting source identities')
            frozen[p]=h
    for name,h in baseline['generated_files_sha256'].items():frozen[base/name]=h
    if sha(a.exe)!=baseline['identity']['executable_sha256']:raise ValueError('Executable identity changed')
    frozen[a.exe.resolve()]=sha(a.exe)
    sources=[p for p in frozen if p.name=='x87_stack_candidate.c']
    if len(sources)!=1:raise ValueError('Missing unique verified candidate source')
    original=Path(__file__).resolve().parents[1]/'native/x87_stack.c'
    old='uint16_t value=(uint16_t)(bytes[0]|(uint16_t)bytes[1]<<8);'
    new='uint16_t value=(uint16_t)(((bytes[0]|(uint16_t)bytes[1]<<8)&0x1f3fu)|0x40u);'
    if original.read_text().count(old)!=1 or sources[0].read_text()!=original.read_text().replace(old,new):
        raise ValueError('Candidate is not solely the audited control-word normalization')
    objects=[]
    for item in baseline['compiled']:
        p=base/item['object'];frozen[p]=item['object_sha256'];objects.append(p)
    replacements=0
    for item in core['objects']:
        p=Path(item['object']).resolve();frozen[p]=item['sha256']
        if Path(item['source']).name=='x87_stack.c':replacements+=1;continue
        objects.append(p)
    if replacements!=1:raise ValueError('Expected exactly one stack object')
    for p in (a.baseline.resolve(),a.host_core.resolve(),a.candidate_proof.resolve(),Path(__file__).resolve()):frozen[p]=sha(p)
    def verify():
        if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Changed experiment input')
    verify()
    def run(args,name,timeout=120):
        r=subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=timeout)
        (out/name).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+': '+r.stderr[-3000:])
        return r.stdout
    obj=out/'x87_stack_candidate.o'
    run([*core['compiler'],*core['flags'],'-c',sources[0],'-o',obj],'compile.log')
    objects.append(obj);exe=out/'candidate-test.exe'
    response=out/'link.rsp';arg=object_response(response,objects,exe)
    run([*core['compiler'],arg],'link.log')
    arguments=[exe,a.exe.resolve(),a.exe.resolve().parent]
    if baseline.get('text_data'):
        captured=base/'captured-text-data.bin'
        if sha(captured)!=baseline['text_data']['sha256']:raise ValueError('Changed captured data')
        frozen[captured]=sha(captured);arguments.append(captured)
    host=json.loads(run(arguments,'host.log').strip().splitlines()[-1]);verify()
    result=dict(status='CANDIDATE_ONLY_NOT_PACKAGING_EVIDENCE',host=host,
        replaced_core_object='x87_stack.c',candidate_object_sha256=sha(obj),candidate_executable_sha256=sha(exe),
        baseline_sha256=sha(a.baseline),candidate_proof_sha256=sha(a.candidate_proof),
        original_generated_objects_recompiled=False,engine_started=False,hardware_tested=False,
        production_sources_modified=False,private_input_sha256={str(p):h for p,h in frozen.items()})
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='private_input_sha256'},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('baseline','host-core','candidate-proof','exe','output'):p.add_argument('--'+name,type=Path,required=True)
    probe(p.parse_args())

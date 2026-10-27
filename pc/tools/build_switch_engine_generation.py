"""Rebuild audited private generated units with the current Switch runtime ABI.

Recompiles generated code only. Old native runtime objects are excluded.
Outputs contain game-derived code and must remain private.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor,as_completed
import json
from pathlib import Path
import re
import subprocess
import time
from build_cpu_runtime import SWITCH_FLAGS
from run_aot_analysis import sdk_identity,sha
from x87_models import MODELS,model_flags


ABI_HEADERS=('fnv_sdk_runtime.h','fnv_mul_flags.h','fnv_simd_bits.h','x87_stack.h',
             'x87_extended.h','x87_transcendental_candidate.h','x87_trig_candidate.h')


def build(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve();generation=a.generation.resolve()
    if out.exists() or not 1<=a.jobs<=8:raise ValueError('Require a fresh output and one to eight jobs')
    old=json.loads(a.cpu_validation.read_text());runtime=json.loads(a.runtime_validation.read_text())
    flags=runtime.get('compiler_flags')
    allowed=[[*SWITCH_FLAGS,'-DFNV_CPU_EXTENDED_X87=1',*model_flags(m)] for m in MODELS]
    if (old.get('status')!='PASS' or runtime.get('status')!='PASS' or flags not in allowed or
        flags!=old.get('compiler_flags') or runtime.get('generated_cpu_objects_reused') is not False):
        raise ValueError('CPU generation and current runtime ABI disagree')
    sdk=a.sdk.resolve();sdk_hash=sdk_identity(sdk)
    if sdk_hash!=old['sdk_sha256'] or sdk_hash!=runtime['sdk_sha256']:
        raise ValueError('SDK identity mismatch')
    sources=[]
    for row in old['compiled']:
        name=Path(row['source']).name
        if name in ('cpu_runtime.c','pe_image.c'):continue
        if not re.fullmatch(r'recomp_(?:\d{4}|dispatch|stubs_unresolved)\.c',name):
            raise ValueError('Unexpected generated unit: '+name)
        if name in [p.name for p in sources]:raise ValueError('Duplicate generated unit')
        sources.append(generation/name)
    if not sources:raise ValueError('Empty generation')
    present={p.name for p in generation.glob('*.c') if re.fullmatch(r'recomp_(?:\d{4}|dispatch|stubs_unresolved)\.c',p.name)}
    if present!={p.name for p in sources}:raise ValueError('Generated unit inventory differs from the CPU proof')
    nx=a.libnx.resolve();cc=a.cc.resolve()
    frozen={str(p):sha(p) for p in [*sources,generation/'recomp_funcs.h',cc,
            *[pc/'native'/n for n in ABI_HEADERS]]}
    for name,digest in frozen.items():
        if old['source_sha256'].get(name)!=digest:raise ValueError('Changed generation ABI input: '+name)
    for p in [cc,*[pc/'native'/n for n in ABI_HEADERS]]:
        if runtime['source_sha256'].get(str(p))!=sha(p):raise ValueError('Changed runtime ABI input: '+str(p))
    for p in [a.cpu_validation.resolve(),a.runtime_validation.resolve(),Path(__file__).resolve(),
              pc/'tools/build_cpu_runtime.py',pc/'tools/run_aot_analysis.py',pc/'tools/x87_models.py',
              *sorted((sdk/'templates/runtime').rglob('*.h')),*sorted((sdk/'src/kernel').rglob('*.h')),
              *sorted((nx/'include').rglob('*.h'))]:frozen[str(p)]=sha(p)
    out.mkdir(parents=True);started=time.monotonic()

    def compile_unit(source):
        obj=out/(source.stem+'.o')
        command=[str(cc),*flags,*['-I'+str(p) for p in (pc/'native',generation,sdk/'templates/runtime',sdk/'src/kernel',nx/'include')],
                 '-include',str(pc/'native/fnv_sdk_runtime.h'),'-c',str(source),'-o',str(obj)]
        begin=time.monotonic();r=subprocess.run(command,capture_output=True,text=True,timeout=600)
        (out/(source.stem+'.log')).write_text(r.stdout+r.stderr)
        return dict(source=str(source),object=str(obj),sha256=sha(obj) if obj.exists() else None,
                    exit_code=r.returncode,seconds=round(time.monotonic()-begin,3),command=command)

    compiled=[]
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for future in as_completed([pool.submit(compile_unit,p) for p in sources]):
            row=future.result();compiled.append(row)
            progress=dict(completed=len(compiled),total=len(sources),
                          failed=[r['source'] for r in compiled if r['exit_code']],
                          elapsed_seconds=round(time.monotonic()-started),last=row['source'])
            temporary=out/'progress.tmp';temporary.write_text(json.dumps(progress,indent=2)+'\n');temporary.replace(out/'progress.json')
            if len(compiled)%20==0 or row['exit_code']:
                print('ARM64 units',len(compiled),'/',len(sources),'failures',len(progress['failed']),flush=True)
    if sdk_identity(sdk)!=sdk_hash or any(sha(Path(p))!=h for p,h in frozen.items()):
        raise ValueError('Generation inputs changed during compilation')
    result=dict(status='COMPILED' if all(not r['exit_code'] for r in compiled) else 'COMPILE_FAILURE',
                compiled=sorted(compiled,key=lambda r:r['source']),source_sha256=frozen,compiler_flags=flags,
                elapsed_seconds=round(time.monotonic()-started),sdk_sha256=sdk_hash,
                old_native_objects_reused=False,full_engine_linked=False,nro_created=False,
                game_started=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('cpu-validation','runtime-validation','generation','sdk','cc','libnx','output'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--jobs',type=int,default=3)
    result=build(p.parse_args())
    print(result['status'],len(result['compiled']))
    if result['status']!='COMPILED':raise SystemExit(1)

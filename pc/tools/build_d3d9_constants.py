"""Integrate the constants module with an unchanged accepted Windows/CPU bundle.

Private relocatable link only: no COM dispatch, renderer or NRO is produced.
"""
import argparse
import json
from pathlib import Path
from build_cpu_runtime import SWITCH_FLAGS,PLATFORM_SYMBOLS,run
from run_aot_analysis import sha

def build(a):
    if a.output.exists():raise ValueError('Use a fresh private output directory')
    pc=Path(__file__).resolve().parents[1];base=json.loads(a.base_validation.read_text())
    if (base['status']!='PASS' or not base['memory_exports'] or base['new_compile_units']!=4 or
            base['partial_link']['unexpected_symbols'] or not base['platform_definitions_verified'] or
            base['compiler_flags']!=SWITCH_FLAGS):raise ValueError('Require the accepted Windows/VM bundle')
    frozen={Path(p):h for p,h in base['source_sha256'].items()}
    frozen[a.base_validation.resolve()]=sha(a.base_validation)
    partial=a.base_validation.resolve().parent/'fnv-windows-thread-relocatable.o'
    frozen[partial]=base['partial_link']['object_sha256']
    if partial.stat().st_size!=base['partial_link']['object_bytes']:raise ValueError('Changed bundle size')
    for item in base['compiled']:
        if Path(item['source']).name!=item['source']:raise ValueError('Invalid object path')
        obj=partial.parent/(Path(item['source']).stem+'.o')
        if obj.stat().st_size!=item['object_bytes']:raise ValueError('Changed object size')
        frozen[obj]=item['object_sha256']
    for binary in (a.cc,a.ld,a.nm):
        if frozen.get(binary.resolve())!=sha(binary):raise ValueError('Tool differs from accepted bundle')
    if not (a.libnx/'include/switch/kernel/mutex.h').is_file():raise ValueError('Missing libnx headers')
    for p in (a.libnx.resolve()/'include').rglob('*.h'):
        if frozen.get(p)!=sha(p):raise ValueError('Changed libnx headers')
    for name in ('d3d9_constants.c','d3d9_constants.h','host_mutex.h'):
        p=pc/'native'/name;h=sha(p)
        if p in frozen and frozen[p]!=h:raise ValueError('Changed shared header')
        frozen[p]=h
    frozen[Path(__file__).resolve()]=sha(Path(__file__).resolve())
    proof_path=partial.parent/'platform-symbols.json';proof=json.loads(proof_path.read_text())
    if proof['status']!='PASS' or proof['missing']:raise ValueError('Invalid platform proof')
    frozen[proof_path]=sha(proof_path)
    for name,h in proof['libraries'].items():
        if frozen.get(Path(name))!=h:raise ValueError('Library identity mismatch')
    def unchanged():return all(sha(p)==h for p,h in frozen.items())
    if not unchanged():raise ValueError('Changed accepted input/object')
    out=a.output.resolve();out.mkdir(parents=True);obj=out/'d3d9_constants.o'
    command=[a.cc.resolve(),*SWITCH_FLAGS,'-Wall','-Wextra','-Werror','-I',pc/'native',
        '-I',a.libnx.resolve()/'include','-c',pc/'native/d3d9_constants.c','-o',obj]
    run(command,out/'compiler.log');linked=out/'fnv-d3d9-constants-relocatable.o'
    run([a.ld.resolve(),'-r','-o',linked,partial,obj],out/'link.log')
    text=run([a.nm.resolve(),'--undefined-only','--format=posix','--no-demangle',linked],out/'undefined-symbols.log')
    undefined=sorted({line.split()[0] for line in text.splitlines() if line.strip()})
    unexpected=sorted(set(undefined)-(PLATFORM_SYMBOLS|{'malloc','memcmp','mutexLock','mutexUnlock','svcSetMemoryPermission'}))
    definitions={symbol:[] for symbol in undefined}
    for index,name in enumerate(proof['libraries']):
        text=run([a.nm.resolve(),'--defined-only','--extern-only','--format=posix','--no-demangle',Path(name)],out/f'platform-{index}.log')
        names={parts[0] for line in text.splitlines() if len(parts:=line.split())>=2 and parts[1] not in ('U','u','w','v')}
        for symbol in definitions:
            if symbol in names:definitions[symbol].append(name)
    missing=sorted(symbol for symbol,providers in definitions.items() if not providers)
    if not unchanged():raise ValueError('Input changed during integration')
    result=dict(status='PASS' if not unexpected and not missing else 'SYMBOL_FAILURE',
        base_compile_units_reused=base['total_compile_units'],new_compile_units=1,total_compile_units=base['total_compile_units']+1,
        source_sha256={str(p):h for p,h in frozen.items()},compiler_flags=SWITCH_FLAGS,
        compiled=[dict(source='d3d9_constants.c',object_bytes=obj.stat().st_size,object_sha256=sha(obj),command=[str(x) for x in command])],
        partial_link=dict(object_bytes=linked.stat().st_size,object_sha256=sha(linked),undefined_symbols=undefined,unexpected_symbols=unexpected,final_engine_link=False),
        platform_definitions=definitions,missing_platform_definitions=missing,
        native_engine_started=False,d3d9_com_connected=False,rendered=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('base-validation','libnx','cc','ld','nm','output'):p.add_argument('--'+n,type=Path,required=True)
    r=build(p.parse_args());print(json.dumps({k:v for k,v in r.items() if k not in ('source_sha256','compiled','platform_definitions')},indent=2))
    if r['status']!='PASS':raise SystemExit(1)

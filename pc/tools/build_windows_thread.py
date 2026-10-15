"""Add native Windows thread services to an unchanged, validated CPU bundle.

Reuses the relocatable base only after checking its inputs and every compiled
object. A partial link is not a game executable, NRO or native boot result.
"""
import argparse
import json
from pathlib import Path
import re
import sys
from aot_code_data import verify_derivation
from build_cpu_runtime import SWITCH_FLAGS, PLATFORM_SYMBOLS, run
from run_aot_analysis import sdk_identity, sha


def build(a):
    if a.output.exists():raise ValueError('Use a fresh private output directory')
    pc=Path(__file__).resolve().parents[1];base=json.loads(a.base_validation.read_text())
    if base['status']!='PASS' or base['partial_link']['unexpected_symbols']:
        raise ValueError('Require a successful complete CPU bundle')
    frozen={Path(name):digest for name,digest in base['source_sha256'].items()}
    frozen[a.base_validation.resolve()]=sha(a.base_validation)
    partial=a.base_validation.resolve().parent/'fnv-cpu-relocatable.o'
    frozen[partial]=base['partial_link']['object_sha256']
    if len(base['compiled'])!=base['compile_units'] or base['compiler_flags']!=SWITCH_FLAGS:
        raise ValueError('Incomplete or incompatible base inventory')
    for item in base['compiled']:
        if item['exit_code'] or Path(item['source']).name!=item['source']:
            raise ValueError('Invalid compiled unit')
        obj=partial.parent/(Path(item['source']).stem+'.o')
        if obj.stat().st_size!=item['object_bytes']:raise ValueError('Changed object size')
        frozen[obj]=item['object_sha256']
    for binary in (a.cc,a.ld,a.nm,a.objdump):
        if frozen.get(binary.resolve())!=sha(binary):raise ValueError('Tool differs from the pinned base input')
    if sdk_identity(a.sdk.resolve())!=base['sdk_sha256']:raise ValueError('Changed SDK')
    for directory in (a.sdk.resolve()/'templates/runtime',a.sdk.resolve()/'src/kernel'):
        for header in directory.rglob('*.h'):
            if frozen.get(header)!=sha(header):raise ValueError('SDK headers are not the pinned base inputs')
    sys.path.insert(0,str(a.python_deps.resolve()));import capstone
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
    selection=verify_derivation(a.analysis.resolve(),decoder)
    if not selection:raise ValueError('Require the verified code/data selection')
    reports=[path for path in a.analysis.glob('generation-report-*.json')
        if sha(path)==base['generation_report_sha256']]
    if len(reports)!=1 or json.loads(reports[0].read_text())['code_data_filter']!=selection:
        raise ValueError('Analysis is not the pinned base generation')
    for path in (pc/'native/windows_thread.c',pc/'native/windows_thread.h',Path(__file__).resolve()):
        frozen[path]=sha(path)
    if any(sha(p)!=digest for p,digest in frozen.items()):raise ValueError('Changed base input/object')
    output=a.output.resolve();output.mkdir(parents=True)
    obj=output/'windows_thread.o';linked=output/'fnv-windows-thread-relocatable.o'
    command=[a.cc.resolve(),*SWITCH_FLAGS,'-Wall','-Wextra','-Werror','-include',pc/'native/fnv_sdk_runtime.h']
    for directory in (pc/'native',a.sdk.resolve()/'templates/runtime',a.sdk.resolve()/'src/kernel'):
        command+=['-I',directory]
    run(command+['-c',pc/'native/windows_thread.c','-o',obj],output/'compiler.log')
    run([a.ld.resolve(),'-r','-o',linked,partial,obj],output/'link.log')
    symbols=run([a.nm.resolve(),'--undefined-only','--format=posix','--no-demangle',linked],output/'undefined-symbols.log')
    undefined=[line.split()[0] for line in symbols.splitlines() if line.strip()]
    # The new module uses malloc for a temporary merged export table.
    unexpected=sorted(set(undefined)-(PLATFORM_SYMBOLS|{'malloc'}))
    assembly=run([a.objdump.resolve(),'-dr',obj],output/'windows-thread.asm')
    tls_ok='__aarch64_read_tp' in assembly and not re.search(r'\bmrs\b[^\n]*tpidr_el[01]',assembly,re.I)
    if (any(sha(p)!=digest for p,digest in frozen.items()) or
        verify_derivation(a.analysis.resolve(),decoder)!=selection or sdk_identity(a.sdk.resolve())!=base['sdk_sha256']):
        raise ValueError('Inputs changed during integration')
    result=dict(status='PASS' if not unexpected and tls_ok else 'ABI_SYMBOL_FAILURE',
        base_compile_units_reused=base['compile_units'],new_compile_units=1,total_compile_units=base['compile_units']+1,
        base_validation_sha256=sha(a.base_validation),source_sha256={str(p):digest for p,digest in frozen.items()},
        compiler_flags=SWITCH_FLAGS,tls_soft_tp_verified=bool(tls_ok),
        object_sha256=sha(obj),partial_link=dict(object_bytes=linked.stat().st_size,object_sha256=sha(linked),
            undefined_symbols=undefined,unexpected_symbols=unexpected,final_engine_link=False),
        native_engine_started=False,cold_boot_supported=False)
    (output/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for n in ('base-validation','analysis','sdk','python-deps','cc','ld','nm','objdump','output'):
        parser.add_argument('--'+n,type=Path,required=True)
    result=build(parser.parse_args())
    print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))
    if result['status']!='PASS':raise SystemExit(1)

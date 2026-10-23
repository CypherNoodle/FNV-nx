"""Replace CPU implementation and integrate COM, reusing unchanged guest C.

The old CPU C/header must be supplied exactly as recorded by the accepted
baseline. Generated units may not include that header, even transitively.
No SDK, generated body or shared SDK ABI header may change. Private link only.
"""
import argparse
import json
from pathlib import Path
import re
import sys
from aot_code_data import verify_derivation
from build_cpu_runtime import SWITCH_FLAGS,PLATFORM_SYMBOLS,run
from run_aot_analysis import sdk_identity,sha

def build(a):
    if a.output.exists():raise ValueError('Use a fresh private output directory')
    pc=Path(__file__).resolve().parents[1];base=json.loads(a.base_validation.read_text())
    if base['status']!='PASS' or base['partial_link']['unexpected_symbols'] or base['compiler_flags']!=SWITCH_FLAGS:
        raise ValueError('Require an accepted Switch CPU baseline')
    if len(base['compiled'])!=base['compile_units']:raise ValueError('Incomplete object inventory')
    frozen={Path(p):h for p,h in base['source_sha256'].items()}
    replaced={}
    for name,archive in (('cpu_runtime.c',a.previous_cpu_source),('cpu_runtime.h',a.previous_cpu_header)):
        path=pc/'native'/name;old=frozen[path]
        if sha(archive)!=old:raise ValueError('Archived CPU input differs from baseline')
        frozen[archive.resolve()]=old;frozen[path]=sha(path);replaced[name]=dict(before=old,after=frozen[path])
    frozen[a.base_validation.resolve()]=sha(a.base_validation)
    for binary in (a.cc,a.ld,a.nm,a.objdump):
        if frozen.get(binary.resolve())!=sha(binary):raise ValueError('Changed toolchain')
    sdk=a.sdk.resolve()
    if sdk_identity(sdk)!=base['sdk_sha256']:raise ValueError('Changed SDK')
    sys.path.insert(0,str(a.python_deps.resolve()));import capstone
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
    selection=verify_derivation(a.analysis.resolve(),decoder)
    reports=[p for p in a.analysis.glob('generation-report-*.json') if sha(p)==base['generation_report_sha256']]
    if not selection or len(reports)!=1 or json.loads(reports[0].read_text())['code_data_filter']!=selection:
        raise ValueError('Generation identity/selection mismatch')
    reused=[];checked=0;old_cpu=0
    for item in base['compiled']:
        if item['exit_code'] or Path(item['source']).name!=item['source']:raise ValueError('Invalid compiled unit')
        obj=a.base_validation.resolve().parent/(Path(item['source']).stem+'.o')
        if obj.stat().st_size!=item['object_bytes']:raise ValueError('Changed object size')
        frozen[obj]=item['object_sha256'];checked+=1
        if item['source']=='cpu_runtime.c':old_cpu+=1
        else:reused.append(obj)
    if old_cpu!=1:raise ValueError('Expected exactly one old CPU implementation')
    # All guest C and all possible non-system headers reachable from it are
    # baseline-pinned. Literal-only includes make this exclusion check useful;
    # reject macro includes rather than guessing their dependency graph.
    excluded={pc/'native/cpu_runtime.h',a.previous_cpu_source.resolve(),a.previous_cpu_header.resolve()}
    dependency_inputs=[p for p in frozen if p.suffix=='.h' and p not in excluded]
    dependency_inputs += [p for p in frozen if p.suffix=='.c' and p.name!='cpu_runtime.c' and p not in excluded]
    for path in dependency_inputs:
        for line in path.read_text(errors='strict').splitlines():
            if re.match(r'\s*#\s*include\b',line):
                if not re.match(r'\s*#\s*include\s*[<"]',line) or 'cpu_runtime.h' in line:
                    raise ValueError('Cannot reuse a unit depending on the replaced CPU header: '+str(path))
    sources=['cpu_runtime.c','windows_thread.c','guest_memory.c','memory_horizon.c','windows_memory.c','d3d9_constants.c','d3d9_com.c']
    headers=['windows_thread.h','guest_memory.h','memory_horizon.h','windows_memory.h','d3d9_constants.h','d3d9_com.h','d3d9_dds_texture.inc','bc_mip_chain.inc','dds_image_info.h','host_mutex.h']
    for path in (pc/'third_party/stb_dxt/stb_dxt.h',pc/'third_party/stb_dxt/provenance.json'):frozen[path]=sha(path)
    for name in sources+headers:
        p=pc/'native'/name;frozen[p]=sha(p)
    for p in (a.libnx.resolve()/'include').rglob('*.h'):frozen[p]=sha(p)
    frozen[Path(__file__).resolve()]=sha(Path(__file__).resolve())
    proof=json.loads(a.platform_validation.read_text());frozen[a.platform_validation.resolve()]=sha(a.platform_validation)
    if proof['status']!='PASS':raise ValueError('Invalid platform inventory')
    for name,h in proof['libraries'].items():frozen[Path(name)]=h
    def unchanged():return all(sha(p)==h for p,h in frozen.items())
    if not unchanged():raise ValueError('Changed accepted input/object')
    out=a.output.resolve();out.mkdir(parents=True);objects=[];compiled=[]
    for name in sources:
        src=pc/'native'/name;obj=out/(src.stem+'.o');objects.append(obj)
        command=[a.cc.resolve(),*SWITCH_FLAGS,'-Wall','-Wextra','-Werror']
        if name not in ('guest_memory.c','memory_horizon.c','d3d9_constants.c'):
            command+=['-include',pc/'native/fnv_sdk_runtime.h']
        for d in (pc/'native',sdk/'templates/runtime',sdk/'src/kernel',a.libnx.resolve()/'include'):command+=['-I',d]
        command+=['-c',src,'-o',obj];run(command,out/(src.stem+'.log'))
        compiled.append(dict(source=name,object_bytes=obj.stat().st_size,object_sha256=sha(obj),command=[str(x) for x in command]))
    linked=out/'fnv-d3d9-com-relocatable.o'
    # A response file avoids Windows argv limits; no original CPU object is linked.
    response=out/'objects.rsp';response.write_text('\n'.join('"'+str(p).replace('\\','/')+'"' for p in reused+objects)+'\n')
    run([a.ld.resolve(),'-r','-o',linked,'@'+str(response)],out/'link.log')
    text=run([a.nm.resolve(),'--undefined-only','--format=posix','--no-demangle',linked],out/'undefined-symbols.log')
    undefined=sorted({line.split()[0] for line in text.splitlines() if line.strip()})
    unexpected=sorted(set(undefined)-(PLATFORM_SYMBOLS|{'malloc','memcmp','memmove','mutexLock','mutexUnlock','svcSetMemoryPermission'}))
    definitions={name:[] for name in undefined}
    for index,name in enumerate(proof['libraries']):
        text=run([a.nm.resolve(),'--defined-only','--extern-only','--format=posix','--no-demangle',Path(name)],out/f'platform-{index}.log')
        names={p[0] for line in text.splitlines() if len(p:=line.split())>=2 and p[1] not in ('U','u','w','v')}
        for symbol in definitions:
            if symbol in names:definitions[symbol].append(name)
    missing=sorted(name for name,providers in definitions.items() if not providers);tls={}
    for stem in ('cpu_runtime','windows_thread'):
        text=run([a.objdump.resolve(),'-dr',out/(stem+'.o')],out/(stem+'.asm'))
        tls[stem]='__aarch64_read_tp' in text and not re.search(r'\bmrs\b[^\n]*tpidr_el[01]',text,re.I)
    if not unchanged() or verify_derivation(a.analysis.resolve(),decoder)!=selection or sdk_identity(sdk)!=base['sdk_sha256']:
        raise ValueError('Inputs changed during integration')
    result=dict(status='PASS' if not unexpected and not missing and all(tls.values()) else 'ABI_SYMBOL_FAILURE',
        base_objects_checked=checked,reused_compile_units=len(reused),new_compile_units=len(objects),total_compile_units=len(reused)+len(objects),
        cpu_inputs_replaced=replaced,generated_and_sdk_include_inputs_checked=len(dependency_inputs),
        source_sha256={str(p):h for p,h in frozen.items()},compiler_flags=SWITCH_FLAGS,compiled=compiled,tls_soft_tp_verified=tls,
        partial_link=dict(object_bytes=linked.stat().st_size,object_sha256=sha(linked),undefined_symbols=undefined,unexpected_symbols=unexpected,final_engine_link=False),
        platform_definitions=definitions,missing_platform_definitions=missing,native_engine_started=False,switch_hardware_tested=False,gpu_rendered=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('base-validation','previous-cpu-source','previous-cpu-header','platform-validation','analysis','sdk','python-deps','libnx','cc','ld','nm','objdump','output'):
        p.add_argument('--'+n,type=Path,required=True)
    r=build(p.parse_args());print(json.dumps({k:v for k,v in r.items() if k not in ('source_sha256','compiled','platform_definitions')},indent=2))
    if r['status']!='PASS':raise SystemExit(1)

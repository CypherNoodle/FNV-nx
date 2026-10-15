"""Verify table-only function selection and compile every generated C unit.

Outputs contain game-derived metadata and objects: keep private. A successful
object build is not a linked engine, import implementation or native boot.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import subprocess
import sys
from aot_code_data import verify_derivation, address
from run_aot_analysis import generation_paths, sdk_identity, sha


def mask_noncode(text):
    token=r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(token,lambda m:re.sub(r'[^\r\n]',' ',m[0]),text,flags=re.S)


def function_hashes(text):
    clean=mask_noncode(text);braces=re.compile(r'[{}]')
    result={};references={}
    for match in re.finditer(r'^void (\w+)\(void\)\s*\{',clean,re.M):
        level=1;close=None
        for brace in braces.finditer(clean,match.end()):
            level+=1 if brace[0]=='{' else -1
            if level==0: close=brace.end();break
        if close is None or match[1] in result: raise ValueError('Malformed/duplicate generated function')
        result[match[1]]=__import__('hashlib').sha256(text[match.start():close].encode()).hexdigest()
        references[match[1]]=set(re.findall(r'\bsub_[0-9A-Fa-f]{8}\b',clean[match.end():close]))
    return result,references


def collect(report, paths):
    functions={};references={}
    for name,digest in report['output_files'].items():
        path=paths['generated']/name
        if sha(path)!=digest: raise ValueError('Modified generated file: '+str(path))
        if re.fullmatch(r'recomp_\d{4}\.c',name):
            hashes,refs=function_hashes(path.read_text())
            if set(functions)&set(hashes): raise ValueError('Duplicate generated definition')
            functions.update(hashes);references.update(refs)
    return functions,references


def dispatch(path):
    text=mask_noncode(path.read_text())
    pairs=re.findall(r'\{\s*0x([0-9A-Fa-f]+)u,\s*\(recomp_func_t\)(\w+)\s*\}',text)
    result={int(a,16):name for a,name in pairs}
    if len(pairs)!=len(result): raise ValueError('Duplicate dispatch address')
    return result


def verify(analysis, name, baseline_analysis, baseline_name, sdk, compiler, deps, output, jobs, linker=None, nm=None):
    if not 1<=jobs<=8: raise ValueError('Use 1..8 compile jobs')
    if nm is not None and linker is None: raise ValueError('--nm requires --ld')
    if output.exists(): raise ValueError('Use a fresh private output directory')
    sys.path.insert(0,str(deps.resolve()));import capstone
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
    selection=verify_derivation(analysis,decoder)
    if not selection or Path(selection['parent_analysis']).resolve()!=baseline_analysis.resolve():
        raise ValueError('Baseline must be the preserved selection parent')
    old_paths=generation_paths(baseline_analysis.resolve(),baseline_name)
    paths=generation_paths(analysis.resolve(),name)
    before=json.loads(old_paths['report'].read_text());after=json.loads(paths['report'].read_text())
    if not before['full_selection'] or not after['full_selection']: raise ValueError('Complete generations required')
    for field in ('input_profile_sha256','sdk_sha256','index_wrapper_sha256','codegen_overlay','multiply_overlay'):
        if before[field]!=after[field]: raise ValueError('Unexpected generation input difference: '+field)
    if after.get('code_data_filter')!=selection or after.get('scan_identity_sha256')!=sha(analysis/'scan-identity.json'):
        raise ValueError('Generation did not validate this table selection')
    sdk_hash=sdk_identity(sdk.resolve())
    if sdk_hash!=after['sdk_sha256']: raise ValueError('SDK changed')
    pc=Path(__file__).resolve().parents[1]
    frozen={}
    for field,stem in (('codegen_overlay','simd_bits'),('multiply_overlay','mul_flags')):
        for key,p in (('source_sha256',pc/'tools'/('aot_'+stem+'.py')),
                      ('runtime_header_sha256',pc/'native'/('fnv_'+stem+'.h'))):
            if sha(p)!=after[field][key]: raise ValueError('Overlay/runtime identity mismatch')
            frozen[p]=sha(p)
    for p in (pc/'tools/aot_code_data.py',pc/'tools/verify_aot_code_data.py',paths['report'],analysis/'scan-identity.json'):
        frozen[p]=sha(p)
    runtime_headers={p:sha(p) for directory in (sdk/'templates/runtime',sdk/'src/kernel') for p in directory.rglob('*.h')}
    frozen.update(runtime_headers)
    compiler_sha=sha(compiler)
    frozen[compiler]=compiler_sha
    for binary in (linker,nm):
        if binary is not None: frozen[binary]=sha(binary)
    old,old_refs=collect(before,old_paths);new,new_refs=collect(after,paths)
    removed={f'sub_{at:08X}' for at in selection['excluded_entries']}
    if set(old)-set(new)!=removed or set(new)-set(old): raise ValueError('Unexpected generated function selection change')
    changed=[n for n in new if new[n]!=old[n]]
    if changed: raise ValueError('Retained function bodies changed: '+', '.join(changed[:12]))
    uses=[n for n,refs in new_refs.items() if refs&removed]
    if uses: raise ValueError('Retained code still references excluded data entries: '+', '.join(uses[:12]))
    old_dispatch=dispatch(old_paths['generated']/'recomp_dispatch.c')
    new_dispatch=dispatch(paths['generated']/'recomp_dispatch.c')
    expected={a:n for a,n in old_dispatch.items() if a not in selection['excluded_entries']}
    if new_dispatch!=expected or len(new)!=after['functions_generated']:
        raise ValueError('Dispatch/body count differs from exact table selection')
    dropped=Counter()
    stub_pattern=r'recomp_unimpl\("untranslated call target",\s*0x([0-9a-fA-F]+)u\)'
    old_targets={int(a,16) for a in re.findall(stub_pattern,(old_paths['generated']/'recomp_stubs_unresolved.c').read_text())}
    new_targets={int(a,16) for a in re.findall(stub_pattern,(paths['generated']/'recomp_stubs_unresolved.c').read_text())}
    if not new_targets<=old_targets or len(old_targets)!=before['gaps']['unresolved_call_targets'] or len(new_targets)!=after['gaps']['unresolved_call_targets']:
        raise ValueError('New or inconsistent unresolved targets')
    for target in old_targets-new_targets:
        symbol=f'sub_{target:08X}'
        if any(symbol in refs and name not in removed for name,refs in old_refs.items()):
            raise ValueError('Dropped unresolved target still has a retained caller')
    audit=json.loads((analysis/'code-data-audit.json').read_text())
    ranges=[(address(e['function']['start']),address(e['function']['end'])) for e in audit['excluded']]
    for family in set(before['gaps']['unsupported_instructions'])|set(after['gaps']['unsupported_instructions']):
        a=Counter(before['gaps']['unsupported_instructions'].get(family,[]));b=Counter(after['gaps']['unsupported_instructions'].get(family,[]))
        if b-a: raise ValueError('New unsupported instruction: '+family)
        if any(not any(lo<=at<hi for lo,hi in ranges) for at in (a-b)):
            raise ValueError('Coverage changed outside excluded data bodies: '+family)
        dropped[family]=sum((a-b).values())
    sources=sorted(n for n in after['output_files'] if n.endswith('.c'))
    output.mkdir(parents=True)
    flags=['-std=gnu11','-O1','-fno-strict-aliasing','-ffp-contract=off','-mearly-ra=none','-Werror=implicit-function-declaration']
    def compile_one(name):
        src=paths['generated']/name;obj=output/(src.stem+'.o')
        command=[str(compiler.resolve()),*flags,'-I',str(sdk.resolve()/'templates/runtime'),'-I',str(sdk.resolve()/'src/kernel'),
            '-I',str(pc/'native'),'-I',str(paths['generated']),'-c',str(src),'-o',str(obj)]
        result=subprocess.run(command,capture_output=True,text=True)
        (output/(src.stem+'.log')).write_text(result.stdout+result.stderr)
        print('compiled' if result.returncode==0 else 'FAILED',name,flush=True)
        return dict(source=name,exit_code=result.returncode,object_sha256=sha(obj) if result.returncode==0 else None,
            object_bytes=obj.stat().st_size if result.returncode==0 else 0)
    with ThreadPoolExecutor(max_workers=jobs) as pool: compiled=list(pool.map(compile_one,sources))
    partial_link=None
    if linker is not None and all(v['exit_code']==0 for v in compiled):
        partial=output/'fnv-generated-relocatable.o';response=output/'link.rsp'
        arguments=['-r','-o',partial.resolve().as_posix()]+[(output/(Path(v['source']).stem+'.o')).resolve().as_posix() for v in compiled]
        if any('"' in s or '\n' in s for s in arguments): raise ValueError('Unsupported response-file path')
        response.write_text('\n'.join('"'+s+'"' for s in arguments)+'\n')
        linked=subprocess.run([str(linker.resolve()),'@'+str(response.resolve())],capture_output=True,text=True)
        (output/'link.log').write_text(linked.stdout+linked.stderr)
        partial_link=dict(exit_code=linked.returncode,object_sha256=sha(partial) if linked.returncode==0 else None,
            object_bytes=partial.stat().st_size if linked.returncode==0 else 0,
            permits_undefined_runtime_symbols=True,final_engine_link=False)
        if linked.returncode==0 and nm is not None:
            symbols=subprocess.run([str(nm.resolve()),'--undefined-only','--format=posix','--no-demangle',str(partial.resolve())],capture_output=True,text=True)
            (output/'undefined-symbols.log').write_text(symbols.stdout+symbols.stderr)
            if symbols.returncode: raise ValueError('Could not inventory undefined runtime symbols')
            partial_link['undefined_symbols']=[line.split()[0] for line in symbols.stdout.splitlines() if line.strip()]
    if sdk_identity(sdk.resolve())!=sdk_hash or any(sha(p)!=h for p,h in frozen.items()) or verify_derivation(analysis,decoder)!=selection:
        raise ValueError('Validation inputs changed')
    collect(after,paths)
    status='PASS' if all(v['exit_code']==0 for v in compiled) else 'COMPILE_FAILURE'
    if partial_link and partial_link['exit_code']!=0: status='RELOCATABLE_LINK_FAILURE'
    if partial_link and any(re.fullmatch(r'sub_[0-9A-Fa-f]{8}',s) for s in partial_link.get('undefined_symbols',[])):
        status='GENERATED_SYMBOL_GAP'
    result=dict(status=status,
        functions_generated=len(new),retained_function_bodies_identical=True,excluded_data_seeds=len(removed),
        unsupported_occurrences_removed_as_data={k:v for k,v in sorted(dropped.items()) if v},
        remaining_unsupported_occurrences=sum(map(len,after['gaps']['unsupported_instructions'].values())),
        unresolved_targets=after['gaps']['unresolved_call_targets'],compile_units=len(compiled),
        compiler_flags=flags,compiled=compiled,selection=selection,
        compiler_sha256=compiler_sha,sdk_runtime_header_sha256={p.relative_to(sdk).as_posix():h for p,h in runtime_headers.items()},
        partial_link=partial_link,
        baseline_report_sha256=sha(old_paths['report']),variant_report_sha256=sha(paths['report']),
        native_engine_started=False,full_game_linked=False,cold_boot_supported=False)
    (output/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('analysis','baseline-analysis','sdk','cc','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--ld',type=Path,help='Optional partial link of all objects; undefined runtime symbols remain allowed')
    p.add_argument('--nm',type=Path,help='Optional undefined-symbol inventory after --ld')
    p.add_argument('--generation-name',required=True);p.add_argument('--baseline-name',required=True);p.add_argument('--jobs',type=int,default=4)
    a=p.parse_args();r=verify(a.analysis,a.generation_name,a.baseline_analysis,a.baseline_name,a.sdk,a.cc,a.python_deps,a.output,a.jobs,a.ld,a.nm)
    print(json.dumps({k:v for k,v in r.items() if k not in ('compiled','selection')},indent=2))
    if r['status']!='PASS':raise SystemExit(1)

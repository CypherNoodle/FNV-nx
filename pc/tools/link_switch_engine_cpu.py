"""Combine private generated CPU objects with the current Switch runtime.

Requires a matching body audit and explicit supplementary-entry manifest.
Produces a relocatable object, not an executable, NRO or hardware result.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from audit_generation_body_bridge import bodies
from run_aot_analysis import sha,sdk_identity


def build(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve()
    if out.exists():raise ValueError('Use a fresh output directory')
    generated=json.loads(a.generated_validation.read_text());runtime=json.loads(a.runtime_validation.read_text())
    old=json.loads(a.cpu_validation.read_text());audit=json.loads(a.body_audit.read_text());manifest=json.loads(a.entries.read_text())
    if (generated.get('status')!='COMPILED' or runtime.get('status')!='PASS' or old.get('status')!='PASS' or
        generated['compiler_flags']!=runtime['compiler_flags'] or generated['compiler_flags']!=old['compiler_flags']):
        raise ValueError('Generation/runtime ABI disagreement')
    if audit.get('status')!='BODY_MATCH' or audit.get('different_functions') or not audit.get('common_functions'):
        raise ValueError('Require the accepted generated-body comparison')
    gen=a.generation.resolve();frozen={}
    for proof in (generated,runtime,audit):
        for name,digest in proof['source_sha256'].items():
            if name in frozen and frozen[name]!=digest:raise ValueError('Dependency proofs disagree: '+name)
            frozen[name]=digest
    def pin(path,digest=None):
        path=path.resolve();value=sha(path) if digest is None else digest;name=str(path)
        if name in frozen and frozen[name]!=value:raise ValueError('Changed proof input: '+name)
        frozen[name]=value
    sdk=a.sdk.resolve()
    if sdk_identity(sdk)!=runtime['sdk_sha256'] or runtime['sdk_sha256']!=old['sdk_sha256']:
        raise ValueError('SDK identity differs from the accepted proofs')
    cc=a.cc.resolve()
    if runtime['source_sha256'].get(str(cc))!=sha(cc):raise ValueError('Compiler differs from the runtime proof')
    expected={Path(r['source']).name for r in old['compiled'] if Path(r['source']).name not in ('cpu_runtime.c','pe_image.c')}
    actual=[Path(r['source']).name for r in generated['compiled']]
    if set(actual)!=expected or len(actual)!=len(expected):raise ValueError('Incomplete generated-object inventory')
    corrections={}
    correction_path=getattr(a,'corrections',None)
    if correction_path:
        for record in json.loads(correction_path.read_text()):
            proof_path=Path(record['proof']).resolve()
            if sha(proof_path)!=record['sha256']:raise ValueError('Changed CFG correction proof')
            corrected=json.loads(proof_path.read_text());key=Path(corrected['original_unit']).resolve()
            kind=corrected.get('status')
            if key in corrections or kind not in ('CFG_CORRECTION_COMPILED','TAIL_OPTIMIZATION_COMPILED') or corrected['compiler_flags']!=generated['compiler_flags'] or corrected['sdk_sha256']!=runtime['sdk_sha256']:
                raise ValueError('Invalid CFG correction metadata')
            if kind=='CFG_CORRECTION_COMPILED':
                if corrected.get('differential_cases',0)<1000 or not corrected.get('legacy_negative_failures'):raise ValueError('Unverified CFG correction')
            elif (corrected.get('optimizer_flags')!=['-foptimize-sibling-calls'] or not corrected.get('source_unchanged') or
                  corrected.get('original_sha256')!=corrected.get('corrected_sha256') or len(corrected.get('verified_edges',[]))<2 or
                  corrected.get('legacy_edges')!='CALL26' or corrected.get('optimized_edges')!='JUMP26'):
                raise ValueError('Unverified tail optimization')
            for name,digest in corrected['source_sha256'].items():
                if name in frozen and frozen[name]!=digest:raise ValueError('Conflicting correction evidence')
                frozen[name]=digest
            pin(proof_path);corrections[key]=corrected
        pin(correction_path)
    objects=[];applied=set()
    for row in generated['compiled']:
        source=Path(row['source']).resolve();obj=Path(row['object']).resolve()
        if source.parent!=gen or row['exit_code'] or sha(obj)!=row['sha256']:
            raise ValueError('Changed or invalid generated object')
        if old['source_sha256'].get(str(source))!=sha(source) or generated['source_sha256'].get(str(source))!=sha(source):
            raise ValueError('Generated source differs from its CPU proof')
        pin(obj,row['sha256'])
        if source in corrections:
            correction=corrections[source]
            if sha(source)!=correction['original_sha256']:raise ValueError('Changed correction baseline')
            corrected_source=Path(correction['corrected_source']);corrected_object=Path(correction['object'])
            if sha(corrected_source)!=correction['corrected_sha256'] or sha(corrected_object)!=correction['object_sha256']:raise ValueError('Changed corrected source/object')
            pin(corrected_source);pin(corrected_object);obj=corrected_object;applied.add(source)
        objects.append(obj)
    if set(corrections)!=applied:raise ValueError('Correction does not belong to generated inventory')
    native=Path(runtime['runtime_object']).resolve()
    if sha(native)!=runtime['runtime_sha256']:raise ValueError('Changed native runtime object')
    pin(native,runtime['runtime_sha256']);objects.append(native)
    entries=manifest.get('entries',[])
    names=[e['function'] for e in entries];addresses=[e['address'] for e in entries]
    if (set(names)!=set(audit['candidate_only']) or len(set(names))!=len(names) or
        len(set(addresses))!=len(addresses) or not entries or len(entries)>64):
        raise ValueError('Supplementary manifest must cover exactly the additional audited bodies')
    for group in manifest.get('required_entry_groups',[]):
        evidence=Path(group['evidence']).resolve()
        if sha(evidence)!=group['sha256']:raise ValueError('Changed required dispatch evidence')
        report=json.loads(evidence.read_text())
        recovered=report.get('recovered_cfg_entries',[])
        required={row['start'] for row in recovered}
        if (report.get('status')!='AUDIT_ONLY_NO_ENTRIES_REGISTERED' or not required or
            len(required)!=len(recovered) or not required<=set(addresses)):
            raise ValueError('Missing required audited dispatch entries')
        pin(evidence)
    dispatch=gen/'recomp_dispatch.c';dispatch_text=dispatch.read_text();selected=[]
    table=re.search(r'\bg_recomp_table\s*\[\s*\]\s*=\s*\{(.*?)^\s*\};',dispatch_text,re.S|re.M)
    if not table:raise ValueError('Unsupported dispatcher table format')
    registered=[int(x,16) for x in re.findall(r'\{\s*0x([0-9A-Fa-f]{8})[uUlL]*\s*,',table.group(1))]
    if len(registered)!=audit['base_functions'] or len(set(registered))!=len(registered) or registered!=sorted(registered):
        raise ValueError('Dispatcher inventory differs from the compared generation')
    for entry in entries:
        name=entry['function'];address=entry['address'];source=Path(entry['source']).resolve()
        if (not re.fullmatch(r'sub_[0-9A-F]{8}',name) or type(address) is not int or
            address!=int(name[4:],16) or not 0<address<0xfe000000):
            raise ValueError('Invalid supplementary function identity')
        if address in registered:
            raise ValueError('Supplementary entry would override the existing dispatcher')
        record=audit['candidate_only'][name]
        if Path(record['source']).resolve()!=source:raise ValueError('Entry source differs from the body audit')
        matches=[body for function,body in bodies(source.read_text()) if function==name]
        if len(matches)!=1 or 'FNV_STARTUP_ICALL(' in matches[0]:
            raise ValueError('Ambiguous entry or unadapted diagnostic call gate')
        body='\n'.join(line.rstrip() for line in matches[0].splitlines()).strip()
        if hashlib.sha256(body.encode()).hexdigest()!=record['sha256']:raise ValueError('Supplementary body changed')
        evidence=Path(entry['boundary_evidence']).resolve()
        if sha(evidence)!=entry['boundary_evidence_sha256']:raise ValueError('Changed boundary evidence')
        pin(evidence);selected.append((address,name,body))
    for p in (a.generated_validation,a.runtime_validation,a.cpu_validation,a.body_audit,a.entries,
              a.cc,a.ld,a.nm,a.readelf,Path(__file__),pc/'tools/audit_generation_body_bridge.py'):
        pin(p)
    if any(sha(Path(p))!=digest for p,digest in frozen.items()):raise ValueError('Build inputs changed before link')
    out.mkdir(parents=True)

    def run(command,name):
        r=subprocess.run(list(map(str,command)),capture_output=True,text=True,timeout=300)
        (out/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+' failed; see its log')
        return r.stdout

    compiled=[]
    for address,name,body in selected:
        source=out/(name+'.c');source.write_text('#include "recomp_funcs.h"\n'+
            ''.join('extern void '+other+'(void);\n' for _,other,_ in selected)+body+'\n')
        compiled.append(source)
    source=out/'supplementary_lookup.c'
    source.write_text('#include "recomp_funcs.h"\nextern recomp_func_t __real_recomp_lookup(uint32_t);\n'+
        ''.join('extern void '+name+'(void);\n' for _,name,_ in selected)+
        'recomp_func_t __wrap_recomp_lookup(uint32_t va){\n'+
        ''.join('if(va=='+hex(address)+'u){if(__real_recomp_lookup(va))abort();return '+name+';}\n' for address,name,_ in selected)+
        'return __real_recomp_lookup(va);\n}\n')
    compiled.append(source)
    for source in compiled:
        obj=source.with_suffix('.o');command=[a.cc.resolve(),*generated['compiler_flags'],
            *['-I'+str(p) for p in (pc/'native',gen,sdk/'templates/runtime',sdk/'src/kernel',a.libnx.resolve()/'include')],
            '-include',pc/'native/fnv_sdk_runtime.h','-c',source,'-o',obj]
        run(command,source.stem);objects.append(obj);pin(source);pin(obj)
    linked=out/'fnv-engine-code-runtime.o';response=out/'link.rsp'
    args=['-r','--wrap=recomp_lookup',*map(str,objects),'-o',str(linked)]
    if any('"' in s or '\n' in s or '\r' in s for s in args):raise ValueError('Unsupported response path')
    response.write_text('\n'.join('"'+s.replace('\\','/')+'"' for s in args)+'\n')
    run([a.ld.resolve(),'@'+str(response)],'link')
    header=run([a.readelf.resolve(),'-h',linked],'header')
    if not re.search(r'Machine:\s+AArch64\b',header):raise ValueError('Wrong combined architecture')
    text=run([a.nm.resolve(),'-u','--format=posix',linked],'undefined')
    undefined={m.group(1) for line in text.splitlines() if (m:=re.match(r'^(\S+)\s+U\b',line))}
    available=set()
    for i,record in enumerate(runtime['archives']):
        archive=Path(record['path'])
        if sha(archive)!=record['sha256']:raise ValueError('Changed runtime archive')
        text=run([a.nm.resolve(),'--defined-only','--extern-only','--format=posix',archive],'archive-'+str(i))
        available.update(m.group(1) for line in text.splitlines() if (m:=re.match(r'^(\S+)\s+[A-TV-Zatv-z]\s+[0-9a-fA-F]+\b',line)))
    missing=sorted(undefined-available)
    if any(sha(Path(p))!=digest for p,digest in frozen.items()):raise ValueError('Build inputs changed during link')
    result=dict(status='PARTIAL_LINKED' if not missing else 'SYMBOL_COVERAGE_FAILURE',
                inputs={name:str(getattr(a,name).resolve()) for name in ('generated_validation','runtime_validation','cpu_validation','body_audit','entries','generation','sdk','libnx','cc','ld','nm','readelf')},
                generated_units=len(generated['compiled']),supplementary_entries=len(selected),cfg_corrected_units=sum(c['status']=='CFG_CORRECTION_COMPILED' for c in corrections.values()),
                tail_optimized_units=sum(c['status']=='TAIL_OPTIMIZATION_COMPILED' for c in corrections.values()),
                runtime_object=str(linked),runtime_sha256=sha(linked),runtime_bytes=linked.stat().st_size,
                undefined_symbols=sorted(undefined),missing_symbols=missing,source_sha256=frozen,
                archive_symbol_coverage_only=True,final_executable_linked=False,nro_created=False,
                game_started=False,switch_hardware_tested=False)
    if correction_path:result['inputs']['corrections']=str(correction_path.resolve())
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('generated-validation','runtime-validation','cpu-validation','body-audit','entries','generation','sdk','libnx','cc','ld','nm','readelf','output'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--corrections',type=Path)
    r=build(p.parse_args())
    print(json.dumps({k:r[k] for k in ('status','generated_units','supplementary_entries','runtime_bytes','missing_symbols','nro_created')}))
    if r['status']!='PARTIAL_LINKED':raise SystemExit(1)

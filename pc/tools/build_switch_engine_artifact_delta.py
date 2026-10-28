"""Private diagnostic delta from retained objects and an exactly reproduced NRO.

This is artifact recovery, not a new audit of historical generated sources.
The ordinary source-audited builder retains its existing admission checks.
The x87 wrapper may change; an optional separately verified unchanged-source
tail-optimization proof permits replacing one generated unit as well.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
from types import SimpleNamespace

from audit_install import pe_info
from build_switch_engine_probe import package, catalog_entries, bundle_membership
from link_switch_engine_platform import sha


def run(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True)
    recipe=json.loads(a.recipe.read_text())
    cfg=SimpleNamespace(**{k:Path(v).resolve() for k,v in recipe.items()})
    pc=Path(__file__).resolve().parents[1]
    frozen={}
    def pin(path,digest=None):
        path=Path(path).resolve();value=sha(path)
        if digest is not None and value!=digest:raise ValueError('Changed artifact: '+str(path))
        if str(path) in frozen and frozen[str(path)]!=value:raise ValueError('Input changed during recovery')
        frozen[str(path)]=value;return path
    pin(a.recipe);pin(Path(__file__));pin(cfg.base_delivery)
    # These headers are read while rebuilding the entry and diagnostics.
    # Freeze current dependencies in addition to the immutable old objects.
    for directory in (pc/'native',pc/'switch',cfg.devkitpro/'libnx/include'):
        for path in sorted(directory.rglob('*.h')):pin(path)
    for path in sorted((pc/'native').glob('*.inc')):pin(path)
    for name in ('switch.specs','switch.ld'):pin(cfg.devkitpro/'libnx'/name)
    delivery=json.loads(cfg.base_delivery.read_text())
    base=pin(cfg.base_nro,delivery['nro_sha256'])
    if delivery['build']!='pc-engine-hw15':raise ValueError('Recovery recipe requires hw15 baseline')
    for n in ('cpu_validation','runtime_validation','platform_validation','bundle_validation','profile','entry_evidence','x87_core'):
        pin(getattr(cfg,n))
    cpu=json.loads(cfg.cpu_validation.read_text());runtime=json.loads(cfg.runtime_validation.read_text())
    platform=json.loads(cfg.platform_validation.read_text());bundle=json.loads(cfg.bundle_validation.read_text())
    profile=json.loads(cfg.profile.read_text());entry=json.loads(cfg.entry_evidence.read_text())
    core=json.loads(cfg.x87_core.read_text())
    if cpu['status']!='PARTIAL_LINKED' or runtime['status']!='PASS' or platform['status']!='FINAL_LINKED':raise ValueError('Invalid baseline receipts')
    pin(cpu['runtime_object'],cpu['runtime_sha256']);pin(runtime['runtime_object'],runtime['runtime_sha256'])
    for proof in (cpu,runtime):
        for name,digest in proof['source_sha256'].items():
            if name.endswith('.o'):pin(name,digest)
    for row in runtime['compiled']:pin(row['object'],row['sha256'])
    for row in runtime['archives']:pin(row['path'],row['sha256'])
    if core['status']!='COMPILED' or not core['switch_abi'] or len(core['objects'])!=runtime['x87_objects']:raise ValueError('Invalid replacement core')
    for name,digest in core['source_sha256'].items():pin(name,digest)
    old_core={Path(n).name:(Path(n),digest) for n,digest in runtime['source_sha256'].items() if n.endswith('.o')}
    changed=[]
    for row in core['objects']:
        p=pin(row['object'],row['sha256']);old=old_core.get(p.name)
        if old is None:raise ValueError('Unexpected replacement object')
        if row['sha256']!=old[1]:changed.append(p.name)
    if changed!=['x87_extended.o']:raise ValueError('Only own x87_extended.o may differ: '+str(changed))
    own_source=pc/'native/x87_extended.c'
    if str(own_source.resolve()) not in core['source_sha256']:raise ValueError('Replacement source not pinned')
    cc=pin(cfg.cc,runtime['source_sha256'][str(cfg.cc)])
    ld=pin(cfg.ld,runtime['source_sha256'][str(cfg.ld)])
    nm=pin(cfg.nm);readelf=pin(cfg.readelf);elf2nro=pin(cfg.elf2nro);nacptool=pin(cfg.nacptool);icon=pin(cfg.icon)
    for tool in (nm,readelf):pin(tool,runtime['source_sha256'][str(tool)])
    mesa=[pin(n,d) for n,d in platform['source_sha256'].items() if Path(n).name=='libvulkan.a']
    if len(mesa)!=1:raise ValueError('Ambiguous Mesa archive')
    archives=[Path(r['path']) for r in runtime['archives'] if Path(r['path']).name not in ('libc.a','libm.a','libgcc.a','libsysbase.a')]
    exe=pin(cfg.exe,profile['executable_sha256']);capture=pin(cfg.capture,profile['text_sha256'])
    if entry['exe_sha256']!=sha(exe) or entry['capture_sha256']!=sha(capture) or not entry['translated_body_control_flow_checked']:raise ValueError('Mismatched entry identity')
    info=pe_info(exe);text=next(s for s in profile['sections'] if s['name']=='.text')
    if info['image_base']!=profile['image_base'] or info['image_bytes']!=profile['image_bytes'] or capture.stat().st_size!=text['virtual_bytes']:raise ValueError('Mismatched PC image')
    imports=[dict(dll=d['dll'],**s) for d in info['imports'] for s in d['symbols']]
    if len(imports)!=profile['normal_import_slots']:raise ValueError('Mismatched imports')
    catalog=pin(cfg.catalog,delivery['embedded']['catalog.sdp']);catalog_codes=catalog_entries(catalog.read_bytes())
    programs=pin(cfg.bundle_validation.parent/'startup_program.h',bundle['header_sha256'])
    if bundle['status']!='PASS' or bundle['programs']!=54:raise ValueError('Invalid shader bundle')
    original_stages=bundle_membership(programs.read_text(),catalog_codes);fixed=pin(cfg.fixed_screen)
    for name,digest in bundle['input_sha256'].items():pin(name,digest)
    nx=cfg.devkitpro/'libnx';port=cfg.devkitpro/'portlibs/switch/lib'
    context=dict(pc=pc,profile=profile,entry=entry,cpu=cpu,runtime=runtime,text=text,imports=imports,
        exe=exe,capture=capture,catalog=catalog,programs=programs,fixed=fixed,nx=nx,port=port,cc=cc,nm=nm,
        readelf=readelf,elf2nro=elf2nro,nacptool=nacptool,icon=icon,archives=archives,mesa=mesa,
        frozen=frozen,original_stages=original_stages,catalog_codes=catalog_codes,pin=pin)
    declared=getattr(a,'entry_source_delta',None) or []
    if len(declared)!=len(set(declared)):raise ValueError('Duplicate entry source delta')
    allowed={'engine_process_plan.c','engine_probe.c'}
    if any(name not in allowed for name in declared):raise ValueError('Unsupported entry source delta')
    declared_paths={str((pc/'switch'/name).resolve()) for name in declared};entry_deltas={}
    # Recreate the delivered binary before admitting any diagnostic change.
    if getattr(a,'reuse_baseline',False):
        baseline_proof=pin(cfg.baseline_output/'validation.json')
        baseline=json.loads(baseline_proof.read_text())
        if baseline.get('status')!='BUILT' or baseline.get('admission')!='exact-nro-artifact-recovery-v1':raise ValueError('Unverified recovered baseline')
        pin(cfg.baseline_output/'fnv-nx.nro',delivery['nro_sha256'])
        pin(cfg.baseline_output/'fnv-nx.elf',baseline['elf_sha256'])
        # Python tooling may evolve; retained compiled inputs and all sources
        # read by the compiler/linker must still match the recovered baseline.
        for name,digest in baseline['source_sha256'].items():
            if name in declared_paths:
                current_digest=sha(pin(name))
                if current_digest==digest:raise ValueError('Declared entry source did not change')
                entry_deltas[name]={'baseline_sha256':digest,'compiled_sha256':current_digest}
            elif Path(name).suffix in ('.c','.h','.inc','.a','.o','.specs','.ld'):pin(name,digest)
        if set(entry_deltas)!=declared_paths:raise ValueError('Entry delta lacks baseline provenance')
    else:
        if cfg.baseline_output.exists():raise ValueError('Baseline recovery requires its original, absent build directory')
        baseline_cfg=SimpleNamespace(**vars(cfg),output=cfg.baseline_output,hardware_revision='hw15',experimental_caps=True)
        baseline=package(baseline_cfg,partial=Path(cpu['runtime_object']),**context)
    if baseline['nro_sha256']!=sha(base):raise ValueError('Baseline NRO did not reproduce exactly; no delta admitted')
    baseline['admission']='exact-nro-artifact-recovery-v1';baseline['fresh_historical_source_audit']=False
    (cfg.baseline_output/'validation.json').write_text(json.dumps(baseline,indent=2)+'\n')
    def relink(response,original,expected,name,replacements=None):
        response=pin(response);args=shlex.split(response.read_text())
        prefix=['-r','--wrap=recomp_lookup'] if name.startswith('cpu') else ['-r']
        if args[:len(prefix)]!=prefix or args[-2]!='-o' or Path(args[-1]).resolve()!=Path(original).resolve():raise ValueError('Unexpected response structure')
        inputs=args[len(prefix):-2]
        for n in inputs:
            p=Path(n).resolve()
            if str(p) not in frozen:raise ValueError('Unpinned response object: '+str(p))
            pin(p,frozen[str(p)])
        target=out/(name+'.o');replacements=replacements or {}
        inputs=[str(replacements.get(str(Path(n).resolve()),Path(n))) for n in inputs]
        rsp=out/(name+'.rsp');rsp.write_text('\n'.join('"'+str(n).replace('\\','/')+'"' for n in [*prefix,*inputs,'-o',str(target)])+'\n')
        r=subprocess.run([str(ld),'@'+str(rsp)],capture_output=True,text=True)
        (out/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError('Relink failed: '+name)
        if expected and sha(target)!=expected:raise ValueError('Original partial did not reproduce: '+name)
        return pin(target)
    native_rsp=cfg.runtime_validation.parent/'runtime-link.rsp';cpu_rsp=cfg.cpu_validation.parent/'link.rsp'
    relink(native_rsp,runtime['runtime_object'],runtime['runtime_sha256'],'native-baseline')
    relink(cpu_rsp,cpu['runtime_object'],cpu['runtime_sha256'],'cpu-baseline')
    replacement=next(Path(r['object']) for r in core['objects'] if Path(r['object']).name=='x87_extended.o')
    native=relink(native_rsp,runtime['runtime_object'],None,'native-delta',{str(old_core['x87_extended.o'][0].resolve()):replacement})
    replacements={str(Path(runtime['runtime_object']).resolve()):native}
    tail=getattr(a,'tail_proof',None)
    if tail:
        proof_path=pin(tail);proof=json.loads(proof_path.read_text())
        if (proof.get('status')!='TAIL_OPTIMIZATION_COMPILED' or proof.get('source_unchanged') is not True or
            proof['original_sha256']!=proof['corrected_sha256'] or proof['optimizer_flags']!=['-foptimize-sibling-calls'] or
            proof['compiler_flags']!=runtime['compiler_flags'] or proof['sdk_sha256']!=runtime['sdk_sha256'] or
            proof.get('legacy_edges')!='CALL26' or proof.get('optimized_edges')!='JUMP26' or len(proof['verified_edges'])<2):
            raise ValueError('Invalid tail optimization proof')
        for name,digest in proof['source_sha256'].items():pin(name,digest)
        generated_path=pin(cpu['inputs']['generated_validation'],cpu['source_sha256'][cpu['inputs']['generated_validation']])
        generated=json.loads(generated_path.read_text())
        rows=[r for r in generated['compiled'] if Path(r['source']).resolve()==Path(proof['original_unit']).resolve()]
        if len(rows)!=1:raise ValueError('Tail source is not a baseline generated unit')
        old=pin(rows[0]['object'],rows[0]['sha256'])
        if cpu['source_sha256'].get(str(old))!=rows[0]['sha256']:raise ValueError('Tail unit was already substituted')
        pin(proof['original_unit'],proof['original_sha256']);pin(proof['corrected_source'],proof['corrected_sha256'])
        replacements[str(old)]=pin(proof['object'],proof['object_sha256'])
        context['cpu']={**cpu,'tail_optimized_units':cpu.get('tail_optimized_units',0)+1}
    partial=relink(cpu_rsp,cpu['runtime_object'],None,'cpu-delta',replacements)
    revision=getattr(a,'hardware_revision','hw16')
    if revision not in ('hw16','hw17','hw18'):raise ValueError('Invalid artifact diagnostic revision')
    delta_cfg=SimpleNamespace(**vars(cfg),output=out/'delta',hardware_revision=revision,experimental_caps=True)
    result=package(delta_cfg,partial=partial,**context)
    result.update(admission='exact-nro-baseline-artifact-delta-v1',fresh_historical_source_audit=False,
        baseline_nro_sha256=sha(base),baseline_elf_sha256=baseline['elf_sha256'],changed_native_objects=changed,
        replacement_core_sha256=sha(cfg.x87_core),tail_optimization_proof_sha256=sha(tail) if tail else None,
        own_entry_source_deltas=entry_deltas,
        gameplay_fix_hardware_verified=False)
    (out/'delta/validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('status','build','nro_sha256','elf_sha256','admission')}))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--recipe',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--reuse-baseline',action='store_true')
    p.add_argument('--tail-proof',type=Path)
    p.add_argument('--hardware-revision',choices=('hw16','hw17','hw18'),default='hw16')
    p.add_argument('--entry-source-delta',action='append',choices=('engine_process_plan.c','engine_probe.c'))
    run(p.parse_args())

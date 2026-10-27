"""Rebuild one private generated unit after a verified bounded CFG correction.

All other function bodies must remain identical. Requires an original-x86 vs
ARM64 differential proof with a failing legacy control. Never publishes code.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from audit_generation_body_bridge import bodies
from run_aot_analysis import sha,sdk_identity

def build(a):
    if a.output.exists():raise ValueError('Use a fresh output directory')
    generated=json.loads(a.generated_validation.read_text());diff=json.loads(a.differential.read_text());runtime=json.loads(a.runtime_validation.read_text())
    if generated.get('status')!='COMPILED' or diff.get('status')!='PASS' or diff.get('differential_cases',0)<1000 or not diff.get('negative_control_failure_count') or not diff.get('all_gpr_compared'):
        raise ValueError('Require real differential verification and a failing legacy control')
    original=a.original_unit.resolve();rows=[r for r in generated['compiled'] if Path(r['source']).resolve()==original]
    if len(rows)!=1 or generated['source_sha256'].get(str(original))!=sha(original):raise ValueError('Original unit differs from accepted generation')
    original_text=original.read_text();old=dict(bodies(original_text));name=f'sub_{a.entry:08X}'
    if name not in old:raise ValueError('Entry does not belong to original unit')
    frozen={str(Path(p).resolve()):digest for p,digest in diff['source_sha256'].items()}
    def pin(path):
        path=Path(path).resolve();value=sha(path)
        if str(path) in frozen and frozen[str(path)]!=value:raise ValueError('Changed differential input')
        frozen[str(path)]=value
    for p in (a.generated_validation,a.runtime_validation,a.differential,original,a.replacement_body,a.legacy_body,a.cc,Path(__file__)):
        pin(p)
    if diff['source_sha256'].get(str(a.replacement_body.resolve()))!=sha(a.replacement_body) or diff['source_sha256'].get(str(a.legacy_body.resolve()))!=sha(a.legacy_body):raise ValueError('Replacement/legacy bodies not covered by differential proof')
    legacy=dict(bodies(a.legacy_body.read_text()))
    if legacy.get(name)!=old[name]:raise ValueError('Differential legacy body is not the original')
    sdk=a.sdk.resolve()
    sdk_hash=runtime['sdk_sha256']
    if diff.get('sdk_sha256')!=sdk_hash:raise ValueError('Differential SDK differs from runtime')
    if sdk_identity(sdk)!=sdk_hash or generated['compiler_flags']!=runtime['compiler_flags'] or runtime['source_sha256'].get(str(a.cc.resolve()))!=sha(a.cc):raise ValueError('SDK/compiler ABI differs from accepted runtime')
    sys.path[:0]=[str(sdk),str(a.python_deps.resolve())]
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    from aot_frame_restore import install as frame
    from aot_simd_bits import install as simd
    from aot_mul_flags import install as multiply
    from aot_x87_codegen import install_profile
    from aot_cfg_decode import install as cfg
    config.configure_from_xbe(str(a.analysis/'fnv-analysis.xbe'));frame();simd();multiply()
    profile=json.loads(a.x87_profile.read_text())
    install_profile(profile);cfg({a.entry})
    db={int(f['start'],0):{**f,'end':int(f['end'],0) if isinstance(f['end'],str) else f['end']} for f in json.loads((a.analysis/'disasm/functions.json').read_text())}
    old_end=db[a.entry]['end'];next_entry=min(x for x in db if x>=old_end and x>a.entry)
    if not old_end<=a.end<=next_entry:raise ValueError('Correction crosses an independently indexed entry')
    db[a.entry]['end']=a.end
    translator=FunctionTranslator((a.analysis/'fnv-analysis.xbe').read_bytes(),db,seh_prolog=0,seh_epilog=0)
    corrected=translator.translate_function(a.entry,db[a.entry])
    expected=dict(bodies(a.replacement_body.read_text()))
    new=dict(bodies(corrected)) if corrected else {}
    if len(new)!=1 or new.get(name)!=expected.get(name) or 'RECOMP_UNIMPL(' in corrected:raise ValueError('Current CFG translation differs from verified body')
    replacement=original_text.replace(old[name],new[name],1)
    after=dict(bodies(replacement))
    if set(after)!=set(old) or any(after[n]!=body for n,body in old.items() if n!=name):raise ValueError('Unrelated function bodies changed')
    for p in (a.analysis/'fnv-analysis.xbe',a.analysis/'input-profile.json',a.analysis/'disasm/functions.json',a.x87_profile):pin(p)
    pc=Path(__file__).resolve().parents[1]
    for n in ('aot_cfg_decode.py','aot_frame_restore.py','aot_simd_bits.py','aot_mul_flags.py','aot_x87_codegen.py','aot_x87_extended.py','aot_sahf_flags.py'):pin(pc/'tools'/n)
    for p in sorted((pc/'native').glob('*.h')):pin(p)
    for name_in_header,digest in generated['source_sha256'].items():
        if Path(name_in_header).parent==original.parent and Path(name_in_header).suffix=='.h':
            if sha(Path(name_in_header))!=digest:raise ValueError('Changed generation header')
            frozen[name_in_header]=digest
    a.output.mkdir(parents=True);out=a.output.resolve();source=out/original.name;source.write_text(replacement,newline='\n');obj=source.with_suffix('.o')
    flags=generated['compiler_flags'];command=[str(a.cc.resolve()),*flags,*['-I'+str(p) for p in (pc/'native',original.parent,sdk/'templates/runtime',sdk/'src/kernel',a.libnx.resolve()/'include')],'-include',str(pc/'native/fnv_sdk_runtime.h'),'-c',str(source),'-o',str(obj)]
    r=subprocess.run(command,capture_output=True,text=True);(out/'compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError('Corrected unit compilation failed: '+r.stderr[-3000:])
    if any(sha(Path(p))!=digest for p,digest in frozen.items()):raise ValueError('Correction inputs changed')
    result=dict(status='CFG_CORRECTION_COMPILED',original_unit=str(original),original_sha256=sha(original),
        corrected_source=str(source),corrected_sha256=sha(source),object=str(obj),object_sha256=sha(obj),
        entry=a.entry,original_end=old_end,corrected_end=a.end,function=name,retained_functions=len(old)-1,
        compiler_flags=flags,command=command,sdk_sha256=sdk_hash,source_sha256=frozen,
        differential_cases=diff['differential_cases'],legacy_negative_failures=diff['negative_control_failure_count'],
        switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('generated-validation','runtime-validation','differential','original-unit','replacement-body','legacy-body','analysis','x87-profile','sdk','python-deps','libnx','cc','output'):p.add_argument('--'+n,type=Path,required=True)
    for n in ('entry','end'):p.add_argument('--'+n,type=lambda s:int(s,0),required=True)
    r=build(p.parse_args());print(json.dumps({k:r[k] for k in ('status','retained_functions','differential_cases','legacy_negative_failures')}))

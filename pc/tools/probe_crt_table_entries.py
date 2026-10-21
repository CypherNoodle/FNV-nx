"""Supplement a frozen desktop CRT diagnostic with audited interior entries.

Original generated objects remain immutable. Only table targets and their bounded, directly verified CFG tails are eligible. This diagnostic extension
is explicitly ineligible as proof for the complete Switch CPU generation.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from audit_crt_math_table import audit
from run_aot_analysis import sha,sdk_identity
from x87_target_evaluator import unchanged


def extend_lookup(text,entries):
    pattern=r'(probe_functions\[\]=\{)(.*?)(\};)'
    match=re.search(pattern,text,re.S)
    if not match:raise ValueError('Missing diagnostic dispatch inventory')
    rows=re.findall(r'\{0x([0-9A-Fa-f]+)u,(\w+)\},',match[2])
    if ''.join('{0x'+a+'u,'+s+'},' for a,s in rows)!=match[2]:raise ValueError('Unexpected diagnostic dispatch syntax')
    table={int(a,16):s for a,s in rows}
    if len(table)!=len(rows) or not table:raise ValueError('Invalid original dispatch')
    if not entries or any(at in table for at in entries):raise ValueError('Cannot override a dispatched entry')
    table.update(entries)
    decl=''.join('void '+symbol+'(void);\n' for symbol in entries.values())
    body=''.join(f'{{0x{at:X}u,{symbol}}},' for at,symbol in sorted(table.items()))
    return decl+text[:match.start(2)]+body+text[match.end(2):]


def probe(a):
    if a.output.exists():raise ValueError('Fresh output required')
    proof=a.baseline.resolve();base=proof.parent;old=json.loads(proof.read_text())
    if (old.get('status')!='EXPERIMENT_STOPPED' or not old.get('all_generated') or old.get('extra_entries') or
        not old.get('host_x87') or old.get('rejected_functions')):raise ValueError('Require complete unsupplemented baseline')
    report=json.loads(a.audit.read_text())
    if report!=audit(a.exe.resolve(),a.analysis.resolve(),report['setup'],report['dispatcher'],a.python_deps):
        raise ValueError('Changed original table evidence')
    eligible=report['recovered_cfg_entries']
    if not eligible:raise ValueError('No proven descriptor CFG entries')
    if sha(a.exe)!=old['identity']['executable_sha256'] or sdk_identity(a.sdk)!=old['sdk_sha256']:
        raise ValueError('EXE/SDK identity mismatch')
    frozen={**old['source_sha256'],str(proof):sha(proof),str(a.audit.resolve()):sha(a.audit),
            str(Path(__file__).resolve()):sha(Path(__file__)),str(a.exe.resolve()):sha(a.exe)}
    for name,digest in old['generated_files_sha256'].items():
        if Path(name).name!=name:raise ValueError('Unsafe original generated path')
        frozen[str(base/name)]=digest
    for i in old['compiled']:frozen[str(base/i['object'])]=i['object_sha256']
    for i in old['host_x87_objects']:frozen[i['object']]=i['sha256']
    unchanged(frozen)
    sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve())]
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    from aot_frame_restore import install as frame
    from aot_simd_bits import install as simd
    from aot_mul_flags import install as multiply
    from aot_x87_codegen import install_profile
    from audit_seh_helpers import verify
    config.configure_from_xbe(str(a.analysis/'fnv-analysis.xbe'));frame();simd();multiply()
    install_profile(old['host_x87']['profile'])
    helpers=verify(a.analysis,json.loads(a.helpers.read_text()));frozen[str(a.helpers.resolve())]=sha(a.helpers)
    db={int(f['start'],0):f for f in json.loads((a.analysis/'disasm/functions.json').read_text())}
    original=(base/'generated_lookup.c').read_text()
    known={int(v,16) for v in re.findall(r'\{0x([0-9A-Fa-f]+)u,\w+\},',original)}
    extras={}
    for entry in eligible:
        at=entry['start'];end=entry['end']
        db[at]=dict(start=hex(at),end=end,size=end-at,name=f'sub_{at:08X}',section='.text',called_by=[])
        extras[at]=f'sub_{at:08X}'
    image=(a.analysis/'fnv-analysis.xbe').read_bytes()
    translator=FunctionTranslator(image,db,seh_prolog=helpers['helpers'][0]['address'],seh_epilog=helpers['helpers'][1]['address'])
    texts=[]
    for at in extras:
        source=translator.translate_function(at,db[at])
        if not source or any(m in source for m in ('translation failed',': unhandled */','recomp_unimpl(')):
            raise ValueError('Supplemental translation is incomplete')
        refs={int(v,16) for v in re.findall(r'\bsub_([0-9A-Fa-f]{8})\b',source)}
        if refs-known-set(extras):raise ValueError('Supplement references an unaudited new target')
        texts.append(source.replace('RECOMP_ICALL_SAFE_AT(', 'FNV_STARTUP_ICALL('))
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    extra=out/'supplemental.c';extra.write_text('#include "generated_functions.h"\n'+''.join('void '+s+'(void);\n' for s in extras.values())+''.join(texts))
    lookup=out/'generated_lookup.c';lookup.write_text(extend_lookup(original,extras))
    cc=json.loads(Path(next(p for p,h in old['source_sha256'].items() if h==old['host_x87']['core_validation_sha256'])).read_text())['compiler']
    includes=['-I'+str(p) for p in (base,pc/'native',a.sdk/'templates/runtime',a.sdk/'src/kernel')]
    def run(args,name):
        r=subprocess.run([str(v) for v in args],capture_output=True,text=True,timeout=120)
        (out/name).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+': '+r.stderr[-4000:])
        return r.stdout
    compiled=[]
    for p in (extra,lookup):
        obj=p.with_suffix('.o');run([*cc,*old['compiler_flags'],*includes,'-include',pc/'native/fnv_sdk_runtime.h','-c',p,'-o',obj],p.stem+'.log')
        compiled.append(dict(source=p.name,object=obj.name,object_sha256=sha(obj)))
    originals=[base/i['object'] for i in old['compiled'] if i['source']!='generated_lookup.c']
    program=out/'startup-test.exe';rsp=out/'link.rsp'
    from probe_captured_startup import object_response
    run([*cc,object_response(rsp,[*originals,*[out/i['object'] for i in compiled],*[Path(i['object']) for i in old['host_x87_objects']]],program)],'link.log')
    shutil.copyfile(base/'captured-text-data.bin',out/'captured-text-data.bin')
    from startup_memory_profile import configuration,validate_observation
    memory=configuration(old['memory_profile'])
    args=[program,a.exe.resolve(),a.exe.resolve().parent,out/'captured-text-data.bin',
          *[str(memory[k]) for k in ('arena_bytes','vm_bytes','commit_limit')],'',str(old['host'].get('worker_stats',{}).get('capacity',0))]
    host=json.loads(run(args,'host.log').strip().splitlines()[-1]);validate_observation(host,old['memory_profile'])
    unchanged(frozen)
    result={**old,'status':'EXPERIMENT_STOPPED' if host['faulted'] else 'EXPERIMENT_RETURNED',
            'host':host,'source_sha256':frozen,'compiled':compiled,'executable_sha256':sha(program),
            'generated_files_sha256':{p.name:sha(p) for p in (extra,lookup,rsp,out/'captured-text-data.bin')},
            'extra_entries':sorted(extras),'generation_matches_all_bodies':False,
            'suitable_for_switch_packaging':False,'reused_compiler_units':len(originals),
            'entry_table_evidence_sha256':sha(a.audit)}
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print('SUPPLEMENTAL_DIAGNOSTIC',len(extras),'audited CFG entries; calls',host['call_count'],'stop',host['text'])
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('baseline','audit','exe','analysis','sdk','python-deps','helpers','output'):p.add_argument('--'+n,type=Path,required=True)
    probe(p.parse_args())

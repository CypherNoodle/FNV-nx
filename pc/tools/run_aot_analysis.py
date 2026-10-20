"""Run private PC static-code analysis with a supplied xboxrecomp SDK tree.
Never launch the game. Generated C and disassembly must remain private.
No title-specific Xbox SDK classifications or forced-success overrides.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def sdk_identity(sdk):
    files={p.relative_to(sdk).as_posix():sha(p) for p in sorted((sdk/"tools").rglob("*.py"))}
    if not files or not (sdk/"tools/recomp/__main__.py").is_file():
        raise ValueError("Expected the supplied SH2/SH3 xboxrecomp tool tree")
    return hashlib.sha256(json.dumps(files,sort_keys=True).encode()).hexdigest()

def generation_gaps(summary, generated):
    markers={"translation_failed":0,"explicit_unimplemented":0,"unhandled_comments":0}
    for file in generated.glob("*.c"):
        code=file.read_text(encoding="utf-8")
        markers["translation_failed"]+=code.count("/* translation failed */")
        markers["explicit_unimplemented"]+=code.count("recomp_unimpl(")
        markers["unhandled_comments"]+=code.count(": unhandled */")
    return dict(failed_functions=summary.get("failed",0),
        unresolved_call_targets=summary.get("unresolved_stubs",0),
        unsupported_instructions=summary.get("unimplemented",{}),code_markers=markers)

def generation_paths(analysis, name=None):
    if name is not None and not re.fullmatch(r'[a-z][a-z0-9-]{0,47}', name):
        raise ValueError('Generation name must be a short lowercase identifier')
    suffix = '-'+name if name is not None else ''
    return {key: analysis/(base+suffix+extension) for key, base, extension in (
        ('generated','generated',''), ('recomp','recomp',''), ('abi','abi',''),
        ('classifications','classifications',''), ('report','generation-report','.json'))}


def verified_scan_wrapper(analysis, scan, current_wrapper):
    expected = scan['index_wrapper_sha256']
    if sha(current_wrapper) == expected:
        return current_wrapper
    archived = analysis/'aot-index-used.py'
    if archived.is_file() and sha(archived) == expected:
        return archived  # Identity proof only: this archive is never executed.
    raise ValueError('Scan wrapper identity unavailable; preserve its exact source or rerun a fresh scan')


def run(analysis, sdk, deps=None, stage="all", max_functions=None, generation_name=None, simd_bits=False, mul_flags=False, frame_restore=False, seh_helpers=None, eh3_helpers=None, x87_profile=None):
    analysis=analysis.resolve(); sdk=sdk.resolve()
    paths = generation_paths(analysis, generation_name)
    if max_functions is not None and max_functions <= 0:
        raise ValueError('--max-functions must be positive')
    if (simd_bits or mul_flags or frame_restore or x87_profile is not None) and (generation_name is None or stage == 'scan'):
        raise ValueError('Codegen overlays require a separate named generation')
    x87_identity=None
    if x87_profile is not None:
        if not frame_restore:raise ValueError('Extended x87 requires the mandatory frame overlay')
        from aot_x87_codegen import load_profile
        x87_config=load_profile(x87_profile)
        pc=Path(__file__).resolve().parents[1]
        dependencies=[Path(__file__).resolve(),pc/'tools/aot_x87_codegen.py',pc/'tools/aot_x87_extended.py',pc/'tools/aot_sahf_flags.py',pc/'tools/x87_models.py',
                      *[pc/'native'/name for name in ('x87_stack.c','x87_stack.h','x87_extended.c','x87_extended.h','cpu_runtime.c','cpu_runtime.h','fnv_sdk_runtime.h')]]
        if x87_config.get('transcendental_model'):
            dependencies.extend(pc/'native'/name for name in ('x87_transcendental_candidate.c','x87_transcendental_candidate.h','x87_quad_bits.h'))
            from x87_models import TRIG_MODEL
            if x87_config['transcendental_model']==TRIG_MODEL:
                dependencies.extend(pc/'native'/name for name in ('x87_trig_candidate.c','x87_trig_candidate.h'))
                dependencies.append(pc/'tools/x87_pi_table.py')
        x87_identity={'profile':x87_config,'profile_sha256':sha(x87_profile),'cpu_compile_define':'FNV_CPU_EXTENDED_X87=1',
                      'source_sha256':{str(path):sha(path) for path in dependencies}}
    helper_audit=None
    eh3_audit=None
    if eh3_helpers is not None:
        if not frame_restore or generation_name is None or stage=='scan':raise ValueError('EH3 helpers require a separate frame-restored generation')
        if deps:sys.path.insert(0,str(deps.resolve()))
        from audit_eh3_helpers import verify as verify_eh3
        eh3_audit=verify_eh3(analysis,json.loads(eh3_helpers.read_text()))
    if seh_helpers is not None:
        if not frame_restore or generation_name is None or stage=='scan':
            raise ValueError('Audited SEH helpers require frame restore and a named generation')
        if deps:sys.path.insert(0,str(deps.resolve()))
        from audit_seh_helpers import verify
        helper_audit=verify(analysis,json.loads(seh_helpers.read_text()))
    if stage != 'scan':
        for key, path in paths.items():
            if path.exists() and (not path.is_dir() or any(path.iterdir())):
                raise ValueError('Generation output already exists: '+str(path))
    scan_manifest=analysis/'scan-identity.json'
    if stage in ('all','scan') and scan_manifest.exists():
        raise ValueError('Scan already recorded; use --stage generate or a fresh analysis directory')
    profile=json.loads((analysis/"input-profile.json").read_text())
    image=analysis/"fnv-analysis.xbe"
    if sha(image)!=profile["analysis_container_sha256"] or not profile["analysis_only"]:
        raise ValueError("Analysis input identity mismatch")
    fingerprint=sdk_identity(sdk)
    index_wrapper=Path(__file__).with_name('aot_index.py')
    index_sha=sha(index_wrapper)
    overlay = Path(__file__).with_name('aot_simd_bits.py')
    mul_overlay = Path(__file__).with_name('aot_mul_flags.py')
    frame_overlay = Path(__file__).with_name('aot_frame_restore.py')
    eh3_overlay = Path(__file__).with_name('aot_eh3_frames.py')
    eh3_identity=({'source_sha256':sha(eh3_overlay),'audit':eh3_audit} if eh3_audit else None)
    frame_identity = {'source_sha256':sha(frame_overlay)} if frame_restore else None
    native = Path(__file__).resolve().parents[1]/'native'
    overlay_identity = ({'source_sha256':sha(overlay), 'runtime_header_sha256':sha(native/'fnv_simd_bits.h')} if simd_bits else None)
    multiply_identity = ({'source_sha256':sha(mul_overlay), 'runtime_header_sha256':sha(native/'fnv_mul_flags.h')} if mul_flags else None)
    environment=os.environ.copy()
    environment["PYTHONUNBUFFERED"]="1"
    environment["PYTHONUTF8"]="1"
    if deps: environment["PYTHONPATH"]=str(deps.resolve())
    def command(name, args):
        print("AOT stage:",name,flush=True)
        with (analysis/(name+".log")).open("w",encoding="utf-8") as log:
            invocation=[sys.executable,"-u","-m",*args]
            if args[0]=='tools.disasm':
                invocation=[sys.executable,"-u",str(index_wrapper),'--sdk',str(sdk),*args[1:]]
            elif args[0]=='tools.recomp' and x87_identity:
                invocation=[sys.executable,'-u',str(Path(__file__).with_name('aot_x87_codegen.py')),'--sdk',str(sdk),'--analysis',str(analysis),'--x87-profile',str(x87_profile.resolve())]
                if eh3_audit:invocation+=['--eh3-helpers',str(eh3_helpers.resolve())]
                if simd_bits:invocation+=['--simd-bits']
                if mul_flags:invocation+=['--mul-flags']
                invocation+=args[1:]
            elif args[0]=='tools.recomp' and eh3_audit:
                invocation=[sys.executable,'-u',str(eh3_overlay),'--sdk',str(sdk),'--analysis',str(analysis),'--eh3-helpers',str(eh3_helpers.resolve())]
                if simd_bits:invocation+=['--simd-bits']
                if mul_flags:invocation+=['--mul-flags']
                invocation+=args[1:]
            elif args[0]=='tools.recomp' and frame_restore:
                invocation=[sys.executable,'-u',str(frame_overlay),'--sdk',str(sdk)]
                if simd_bits: invocation+=['--simd-bits']
                if mul_flags: invocation+=['--mul-flags']
                invocation+=args[1:]
            elif args[0]=='tools.recomp' and mul_flags:
                invocation=[sys.executable,'-u',str(mul_overlay),'--sdk',str(sdk)]
                if simd_bits: invocation += ['--simd-bits']
                invocation += args[1:]
            elif args[0]=='tools.recomp' and simd_bits:
                invocation=[sys.executable,'-u',str(overlay),'--sdk',str(sdk),*args[1:]]
            subprocess.run(invocation,cwd=sdk,env=environment,
                           stdout=log,stderr=subprocess.STDOUT,check=True)
    if stage in ("all","scan"):
        command("parser",["tools.xbe_parser",str(image),"--json",str(analysis/"fnv-analysis_analysis.json"),"--quiet"])
        command("disasm",["tools.disasm",str(image),"--text-only","--force","-v","-o",str(analysis/"disasm")])
        scan_manifest.write_text(json.dumps(dict(input_sha256=sha(image),sdk_sha256=fingerprint,index_wrapper_sha256=index_sha,
            files={p.name:sha(p) for p in (analysis/"disasm").glob("*.json")}),indent=2)+"\n")
        (analysis/'aot-index-used.py').write_bytes(index_wrapper.read_bytes())
    if stage=="scan": return
    generated=paths['generated']
    scan=json.loads(scan_manifest.read_text())
    scan_sha=sha(scan_manifest)
    if scan["input_sha256"]!=sha(image) or scan["sdk_sha256"]!=fingerprint:
        raise ValueError("Disassembly source/SDK identity mismatch; rerun scan")
    verified_wrapper = verified_scan_wrapper(analysis, scan, index_wrapper)
    index_sha = scan['index_wrapper_sha256']
    for name,digest in scan["files"].items():
        if sha(analysis/"disasm"/name)!=digest: raise ValueError("Modified disassembly: "+name)
    code_data_filter=None
    if scan.get('code_data_filter'):
        if generation_name is None: raise ValueError('Filtered selection requires a separate named generation')
        if deps: sys.path.insert(0,str(deps.resolve()))
        import capstone
        from aot_code_data import verify_derivation
        decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32);decoder.detail=True
        code_data_filter=verify_derivation(analysis,decoder)
    # No Xbox kernel/RenderWare signatures are assumed to describe FNV PC.
    classifications=paths['classifications']
    classifications.mkdir(exist_ok=True)
    (classifications/"identified_functions.json").write_text("[]\n")
    suffix = '-'+generation_name if generation_name else ''
    command("abi"+suffix,["tools.abi_analysis",str(image),"--disasm-dir",str(analysis/"disasm"),
        "--func-id-dir",str(classifications),"--output-dir",str(paths['abi'])])
    args=["tools.recomp",str(image),"--all","--split","250","--verbose","--game-name","fnv_pc",
        "--gen-dir",str(generated),"--disasm-dir",str(analysis/"disasm"),
        "--func-id-dir",str(classifications),"--abi-dir",str(paths['abi']),"-o",str(paths['recomp'])]
    if max_functions is not None:
        args += ["--max-funcs",str(max_functions)]
    if helper_audit:
        from audit_seh_helpers import generation_arguments
        args += generation_arguments(analysis,helper_audit)
    command("recomp"+suffix,args)
    if simd_bits or mul_flags:
        header = generated/'recomp_funcs.h'
        code = header.read_text(encoding='utf-8')
        marker = '#include "recomp_types.h"'
        if code.count(marker) != 1:
            raise ValueError('Unexpected generated header; helpers not injected')
        includes = (['fnv_simd_bits.h'] if simd_bits else [])+(['fnv_mul_flags.h'] if mul_flags else [])
        header.write_text(code.replace(marker, marker+''.join('\n#include "'+name+'"' for name in includes)), encoding='utf-8')
    if x87_identity:
        header=generated/'recomp_funcs.h'
        code=header.read_text(encoding='utf-8')
        guard='#ifndef FNV_CPU_EXTENDED_X87\n#error Extended x87 CPU ABI required by this generation\n#endif\n'
        if x87_config.get('transcendental_model'):
            guard+='#ifndef FNV_CPU_X87_QUAD_MATH\n#error Explicit bounded mathematical quad backend required\n#endif\n'
            from x87_models import TRIG_MODEL
            guard+=('#ifndef FNV_CPU_X87_QUAD_TRIG\n#error Explicit true-pi trig backend required\n#endif\n' if x87_config['transcendental_model']==TRIG_MODEL else '#ifdef FNV_CPU_X87_QUAD_TRIG\n#error Non-trig quad profile cannot select trig implicitly\n#endif\n')
        else:
            guard+='#if defined(FNV_CPU_X87_QUAD_MATH) || defined(FNV_CPU_X87_QUAD_TRIG)\n#error Strict x87 profile cannot select quad math implicitly\n#endif\n'
        header.write_text(guard+code,encoding='utf-8',newline='\n')
        if sha(x87_profile)!=x87_identity['profile_sha256'] or any(sha(Path(path))!=digest for path,digest in x87_identity['source_sha256'].items()):
            raise ValueError('Extended x87 input changed during generation')
        # Ignore unused SDK prelude macro definitions, but count actual old
        # global references. Any such path still requires migration.
        legacy={}
        for source in generated.glob('*.c'):
            text=re.sub(r'/\*.*?\*/|//[^\n]*','',source.read_text(),flags=re.S)
            kept=[];continuation=False
            for line in text.splitlines():
                if continuation or line.lstrip().startswith('#'):
                    continuation=line.rstrip().endswith('\\')
                    continue
                kept.append(line)
            text='\n'.join(kept)
            matches=re.findall(r'\bg_fp_(?:stack|top|cmp|control_word|cc)\b',text)
            if matches:legacy[source.name]=len(matches)
        x87_identity['legacy_runtime_references']=legacy
    if sdk_identity(sdk) != fingerprint:
        raise ValueError('SDK source changed during generation')
    if frame_identity and sha(frame_overlay)!=frame_identity['source_sha256']:
        raise ValueError('Frame restore overlay changed during generation')
    if helper_audit:
        verify(analysis,helper_audit)
        if json.loads(seh_helpers.read_text())!=helper_audit:raise ValueError('Helper audit changed during generation')
    if eh3_audit:
        verify_eh3(analysis,eh3_audit)
        if sha(eh3_overlay)!=eh3_identity['source_sha256'] or json.loads(eh3_helpers.read_text())!=eh3_audit:
            raise ValueError('EH3 overlay/audit changed during generation')
    if sha(scan_manifest)!=scan_sha or (code_data_filter and verify_derivation(analysis,decoder)!=code_data_filter):
        raise ValueError('Analysis selection changed during generation')
    for identity, source, runtime in ((overlay_identity,overlay,native/'fnv_simd_bits.h'),
                                     (multiply_identity,mul_overlay,native/'fnv_mul_flags.h')):
        if identity and (sha(source) != identity['source_sha256'] or sha(runtime) != identity['runtime_header_sha256']):
            raise ValueError('Overlay source/runtime changed during generation')
    summary=json.loads((paths['recomp']/"summary.json").read_text())
    if not all(isinstance(summary.get(k),int) and summary[k]>=0 for k in ("total","translated","failed","unresolved_stubs")):
        raise ValueError("Unexpected SDK summary; cannot establish generation coverage")
    gaps=generation_gaps(summary,generated)
    incomplete=any(gaps[k] for k in ("failed_functions","unresolved_call_targets","unsupported_instructions"))
    incomplete |= any(gaps["code_markers"].values())
    if x87_identity:incomplete |= bool(x87_identity['legacy_runtime_references'])
    report=dict(status="GENERATED_WITH_GAPS" if incomplete else "GENERATED_UNVALIDATED",
        input_profile_sha256=sha(analysis/"input-profile.json"),sdk_sha256=fingerprint,index_wrapper_sha256=index_sha,
        generation_name=generation_name, scan_wrapper_verified_from=verified_wrapper.name,
        scan_identity_sha256=scan_sha,code_data_filter=code_data_filter,
        codegen_overlay=overlay_identity,multiply_overlay=multiply_identity,frame_restore_overlay=frame_identity,
        normal_frame_helpers=helper_audit,
        eh3_frame_overlay=eh3_identity,x87_extended_overlay=x87_identity,
        functions_detected=len(json.loads((analysis/"disasm/functions.json").read_text())),
        functions_requested=summary.get("total"),functions_generated=summary.get("translated"),
        max_functions=max_functions,full_selection=max_functions is None,gaps=gaps,
        output_files={p.name:sha(p) for p in generated.iterdir() if p.is_file()},
        native_engine_started=False,cold_boot_supported=False,nro_build_ready=False,
        limits=["Generated C is not yet a compiled/verified engine",
                "Internal ABI recovery is heuristic; native import ABI remains separate",
                "Indirect calls, TLS, exceptions and native API behavior require validation",
                "No failed or untranslated function may be shipped as a silent-success stub"])
    paths['report'].write_text(json.dumps(report,indent=2)+"\n")
    print(report["status"],report["functions_generated"],"functions; nro_build_ready=False",flush=True)
    return report

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--analysis",type=Path,required=True)
    p.add_argument("--sdk",type=Path,required=True)
    p.add_argument("--python-deps",type=Path)
    p.add_argument("--stage",choices=("all","scan","generate"),default="all")
    p.add_argument("--max-functions",type=int)
    p.add_argument('--generation-name', help='Separate outputs/logs for this variant; preserves the baseline')
    p.add_argument('--simd-bits', action='store_true', help='Enable verified SSE2 bits/moves/shuffles overlay')
    p.add_argument('--mul-flags', action='store_true', help='Enable wide multiply and defined CF/OF overlay')
    p.add_argument('--frame-restore', action='store_true', help='Publish restored EBP after POP EBP / LEAVE')
    p.add_argument('--seh-helpers',type=Path,help='Identity-checked normal-frame helper audit, not exception support')
    p.add_argument('--eh3-helpers',type=Path,help='Whole-body-audited additional normal EH3 frame pair')
    p.add_argument('--x87-profile',type=Path,help='Explicit experimental extended-x87 guest profile; separate generation only')
    a=p.parse_args()
    run(a.analysis,a.sdk,a.python_deps,a.stage,a.max_functions,a.generation_name,a.simd_bits,a.mul_flags,a.frame_restore,a.seh_helpers,a.eh3_helpers,a.x87_profile)

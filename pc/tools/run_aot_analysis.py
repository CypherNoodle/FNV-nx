"""Run private PC static-code analysis with a supplied xboxrecomp SDK tree.
Never launch the game. Generated C and disassembly must remain private.
No title-specific Xbox SDK classifications or forced-success overrides.
"""
import argparse
import hashlib
import json
import os
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

def run(analysis, sdk, deps=None, stage="all", max_functions=None):
    analysis=analysis.resolve(); sdk=sdk.resolve()
    profile=json.loads((analysis/"input-profile.json").read_text())
    image=analysis/"fnv-analysis.xbe"
    if sha(image)!=profile["analysis_container_sha256"] or not profile["analysis_only"]:
        raise ValueError("Analysis input identity mismatch")
    fingerprint=sdk_identity(sdk)
    index_wrapper=Path(__file__).with_name('aot_index.py')
    index_sha=sha(index_wrapper)
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
            subprocess.run(invocation,cwd=sdk,env=environment,
                           stdout=log,stderr=subprocess.STDOUT,check=True)
    scan_manifest=analysis/"scan-identity.json"
    if stage in ("all","scan"):
        command("parser",["tools.xbe_parser",str(image),"--json",str(analysis/"fnv-analysis_analysis.json"),"--quiet"])
        command("disasm",["tools.disasm",str(image),"--text-only","--force","-v","-o",str(analysis/"disasm")])
        scan_manifest.write_text(json.dumps(dict(input_sha256=sha(image),sdk_sha256=fingerprint,index_wrapper_sha256=index_sha,
            files={p.name:sha(p) for p in (analysis/"disasm").glob("*.json")}),indent=2)+"\n")
    if stage=="scan": return
    generated=analysis/"generated"
    if generated.exists() and any(generated.iterdir()):
        raise ValueError("Generated directory is not empty; use a fresh private output to avoid stale functions")
    scan=json.loads(scan_manifest.read_text())
    if scan["input_sha256"]!=sha(image) or scan["sdk_sha256"]!=fingerprint or scan['index_wrapper_sha256']!=index_sha:
        raise ValueError("Disassembly source/SDK identity mismatch; rerun scan")
    for name,digest in scan["files"].items():
        if sha(analysis/"disasm"/name)!=digest: raise ValueError("Modified disassembly: "+name)
    # No Xbox kernel/RenderWare signatures are assumed to describe FNV PC.
    classifications=analysis/"classifications"
    classifications.mkdir(exist_ok=True)
    (classifications/"identified_functions.json").write_text("[]\n")
    command("abi",["tools.abi_analysis",str(image),"--disasm-dir",str(analysis/"disasm"),
        "--func-id-dir",str(classifications),"--output-dir",str(analysis/"abi")])
    args=["tools.recomp",str(image),"--all","--split","250","--verbose","--game-name","fnv_pc",
        "--gen-dir",str(analysis/"generated"),"--disasm-dir",str(analysis/"disasm"),
        "--func-id-dir",str(classifications),"--abi-dir",str(analysis/"abi"),"-o",str(analysis/"recomp")]
    if max_functions is not None:
        if max_functions<=0: raise ValueError("--max-functions must be positive")
        args += ["--max-funcs",str(max_functions)]
    command("recomp",args)
    summary=json.loads((analysis/"recomp/summary.json").read_text())
    if not all(isinstance(summary.get(k),int) and summary[k]>=0 for k in ("total","translated","failed","unresolved_stubs")):
        raise ValueError("Unexpected SDK summary; cannot establish generation coverage")
    gaps=generation_gaps(summary,generated)
    incomplete=any(gaps[k] for k in ("failed_functions","unresolved_call_targets","unsupported_instructions"))
    incomplete |= any(gaps["code_markers"].values())
    report=dict(status="GENERATED_WITH_GAPS" if incomplete else "GENERATED_UNVALIDATED",
        input_profile_sha256=sha(analysis/"input-profile.json"),sdk_sha256=fingerprint,index_wrapper_sha256=index_sha,
        functions_detected=len(json.loads((analysis/"disasm/functions.json").read_text())),
        functions_requested=summary.get("total"),functions_generated=summary.get("translated"),
        max_functions=max_functions,full_selection=max_functions is None,gaps=gaps,
        output_files={p.name:sha(p) for p in (analysis/"generated").iterdir() if p.is_file()},
        native_engine_started=False,cold_boot_supported=False,nro_build_ready=False,
        limits=["Generated C is not yet a compiled/verified engine",
                "Internal ABI recovery is heuristic; native import ABI remains separate",
                "Indirect calls, TLS, exceptions and native API behavior require validation",
                "No failed or untranslated function may be shipped as a silent-success stub"])
    (analysis/"generation-report.json").write_text(json.dumps(report,indent=2)+"\n")
    print(report["status"],report["functions_generated"],"functions; nro_build_ready=False",flush=True)
    return report

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--analysis",type=Path,required=True)
    p.add_argument("--sdk",type=Path,required=True)
    p.add_argument("--python-deps",type=Path)
    p.add_argument("--stage",choices=("all","scan","generate"),default="all")
    p.add_argument("--max-functions",type=int)
    a=p.parse_args()
    run(a.analysis,a.sdk,a.python_deps,a.stage,a.max_functions)

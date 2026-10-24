"""Run bounded private Ghidra PE/captured-code analysis with frozen inputs.

Requires an existing trusted Ghidra install and Java. All projects, caches,
settings, decompilation and logs stay under a fresh private output directory.
No runtime code generation or claims of complete function recovery.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(a):
    out=a.output.resolve()
    if out.exists():raise ValueError('Fresh private output directory required')
    exe=a.exe.resolve();capture=a.capture.resolve();ghidra=a.ghidra.resolve()
    if sha(exe)!=a.exe_sha256.lower() or sha(capture)!=a.capture_sha256.lower():
        raise ValueError('EXE/capture identity mismatch')
    if not 1<=len(a.functions)<=32 or not 1<=a.timeout<=600:
        raise ValueError('Use1..32 selected entries and1..600seconds')
    addresses=[int(x,16) for x in a.functions]
    if any(x<0 or x>0xffffffff for x in addresses) or len(set(addresses))!=len(addresses):
        raise ValueError('Require unique32bit function addresses')
    text=int(a.text_va,16)
    if not 0<=text<=0xffffffff or text+capture.stat().st_size>0x100000000:
        raise ValueError('Invalid capture range')
    if any(x<text or x>=text+capture.stat().st_size for x in addresses):
        raise ValueError('Function outside captured code range')
    utility=ghidra/'Ghidra/Framework/Utility/lib/Utility.jar'
    properties=ghidra/'Ghidra/application.properties'
    script=Path(__file__).resolve().parent/'ghidra/AuditCapturedFunctions.java'
    java=Path(shutil.which(a.java) or a.java).resolve()
    for path in (utility,properties,script,java):
        if not path.is_file():raise ValueError('Missing trusted dependency: '+str(path))
    frozen={str(p):sha(p) for p in (exe,capture,utility,properties,script,java,Path(__file__).resolve())}
    out.mkdir(parents=True)
    for name in ('home','tmp','projects'):(out/name).mkdir()
    env=os.environ.copy();env['XDG_CONFIG_HOME']=str(out/'home/config');env['XDG_CACHE_HOME']=str(out/'home/cache')
    command=[str(java),'-Duser.home='+str(out/'home'),'-Djava.io.tmpdir='+str(out/'tmp'),
        '-Djava.system.class.loader=ghidra.GhidraClassLoader',
        '-XX:ErrorFile='+str(out/'jvm-error-%p.log'),
        *(['-XX:TieredStopAtLevel=1'] if getattr(a,'java_c1_only',False) else []),
        '-Xmx2G','-cp',str(utility),
        'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',str(out/'projects'),'PC-Selected',
        '-import',str(exe),'-noanalysis','-scriptPath',str(script.parent),'-postScript',script.name,
        str(capture),a.capture_sha256.lower(),f'{text:x}',str(out/'functions.txt'),
        ','.join(f'{x:08x}' for x in addresses),'-log',str(out/'headless.log'),'-scriptlog',str(out/'script.log')]
    timed_out=False
    with (out/'launch.log').open('w') as log:
        try:child=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,env=env,timeout=a.timeout,cwd=out)
        except subprocess.TimeoutExpired:timed_out=True;child=None
    unchanged=all(sha(Path(p))==h for p,h in frozen.items())
    result=out/'functions.txt';script_log=out/'script.log'
    completed=bool(child and child.returncode==0 and result.is_file() and script_log.is_file()
        and 'PRIVATE_SELECTED_FUNCTION_AUDIT_COMPLETE' in script_log.read_text(errors='replace') and unchanged)
    report=dict(status='PRIVATE_ANALYSIS_COMPLETE' if completed else 'PRIVATE_ANALYSIS_FAILED',
        command=command,exit_code=child.returncode if child else None,timed_out=timed_out,
        source_sha256=frozen,inputs_unchanged=unchanged,
        output_sha256=sha(result) if result.is_file() else None,
        selected_functions=[f'{x:08x}' for x in addresses],complete_function_recovery=False,
        engine_started=False,hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    if not completed:raise RuntimeError('Ghidra analysis failed; inspect private launch/script logs')
    print('PRIVATE_ANALYSIS_COMPLETE',len(addresses),'selected entries; inspect boundaries manually')
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('ghidra','exe','capture','output'):p.add_argument('--'+name,type=Path,required=True)
    for name in ('exe-sha256','capture-sha256','text-va'):p.add_argument('--'+name,required=True)
    p.add_argument('--functions',nargs='+',required=True);p.add_argument('--java',default='java')
    p.add_argument('--java-c1-only',action='store_true',
        help='Disable HotSpot C2 compilation for diagnosing JVM compiler crashes')
    p.add_argument('--timeout',type=int,default=240)
    run(p.parse_args())

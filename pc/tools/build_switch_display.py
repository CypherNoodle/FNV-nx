"""Compile/link the portable display ABI and real libnx resolution query.

Uses current synthetic/Windows proof, never game files. No NRO, automatic
swapchain resize or complete Horizon adapter enumeration is implied.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from build_cpu_runtime import SWITCH_FLAGS
from run_aot_analysis import sdk_identity

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def build(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve()
    if out.exists():raise ValueError('Use fresh output')
    proof=json.loads(a.validation.read_text())
    if proof.get('status')!='PASS' or proof.get('native_display_records_compared',0)<26 or proof.get('strict_flag_stops')!=8 or proof.get('display_metric_checks')!=7 or not proof.get('handheld_dock_policy_checked') or not proof.get('last_error_preserved'):
        raise ValueError('Require accepted display enumeration/metrics/dock proof')
    sdk=a.sdk.resolve();nx=a.libnx.resolve();identity=sdk_identity(sdk)
    if identity!=proof['sdk_sha256']:raise ValueError('SDK differs')
    frozen={str(Path(n).resolve()):h for n,h in proof['source_sha256'].items()}
    sources=[pc/'native'/n for n in ('windows_display.c','windows_user32.c','windows_display_resolution.c')]
    inputs=[*sources,*sorted((pc/'native').glob('*.h')),Path(__file__).resolve(),pc/'tools/build_cpu_runtime.py',pc/'tools/run_aot_analysis.py',a.validation.resolve(),a.cc.resolve(),a.ld.resolve(),a.nm.resolve(),nx/'lib/libnx.a',*sorted((nx/'include').rglob('*.h'))]
    for p in inputs:
        name=str(p.resolve());digest=sha(p)
        if name in frozen and frozen[name]!=digest:raise ValueError('Proof source changed: '+name)
        frozen[name]=digest
    if any(sha(n)!=h for n,h in frozen.items()):raise ValueError('Proof source changed')
    out.mkdir(parents=True);flags=[*SWITCH_FLAGS,'-DFNV_CPU_EXTENDED_X87=1','-Wall','-Wextra','-Werror']
    includes=['-I'+str(p) for p in (pc/'native',sdk/'templates/runtime',sdk/'src/kernel',nx/'include')]
    def run(command,name):
        r=subprocess.run([str(s) for s in command],capture_output=True,text=True,timeout=90)
        (out/name).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+': '+r.stderr)
        return r.stdout
    objects=[]
    for source in sources:
        obj=out/(source.stem+'.o');run([a.cc.resolve(),*flags,*includes,'-include',pc/'native/fnv_sdk_runtime.h','-c',source,'-o',obj],source.stem+'.log');objects.append(obj)
    linked=out/'display-arm64.o';run([a.ld.resolve(),'-r',*objects,'-o',linked],'link.log')
    defined=run([a.nm.resolve(),'--defined-only','-g',linked],'defined.log');symbols={s.split()[-1] for s in defined.splitlines() if s.split()}
    required={'fnv_windows_display_create','fnv_windows_user32_create_with_metrics','fnv_display_resolution_horizon','fnv_display_resolution_metrics','fnv_display_resolution_changed'}
    if not required<=symbols:raise ValueError('Missing display exports')
    undefined=run([a.nm.resolve(),'-u',linked],'undefined.log');pending={s.split()[-1] for s in undefined.splitlines() if s.split()}
    needed_nx={'appletGetOperationMode','appletGetDefaultDisplayResolution','nwindowGetDimensions'}
    nx_output=run([a.nm.resolve(),'--defined-only','-g',nx/'lib/libnx.a'],'libnx.log');nx_symbols={s.split()[-1] for s in nx_output.splitlines() if len(s.split())>=3}
    if not needed_nx<=pending or not needed_nx<=nx_symbols:raise ValueError('Missing actual libnx geometry calls')
    if any(sha(n)!=h for n,h in frozen.items()) or sdk_identity(sdk)!=identity:raise ValueError('Input changed')
    result=dict(status='PASS',source_sha256=frozen,sdk_sha256=identity,compile_units=3,flags=flags,linked_sha256=sha(linked),libnx_geometry_calls=sorted(needed_nx),
        switch_hardware_tested=False,horizon_adapter_catalog_implemented=False,dock_hot_swap_implemented=False,engine_started=False,new_nro=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','libnx','cc','ld','nm','validation','output'):p.add_argument('--'+name,type=Path,required=True)
    build(p.parse_args())

"""Verify and partially link the PC DirectSound/PCM/audout path for Switch.

Accepts only current synthetic PCM/ABI proof. No game generation, EXE or assets
are read, and no NRO or hardware result is produced.
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
    if out.exists():raise ValueError('Use a fresh output directory')
    proof=json.loads(a.fixture_validation.read_text())
    if proof.get('listener_state_checks',0)<508 or proof.get('status')!='PASS' or proof.get('pcm_samples_compared',0)<12592 or proof.get('windows_ABI_checks',0)<10 or proof.get('strict_flag_stops')!=6 or not proof.get('unattached_audio_worker') or not proof.get('full_queue_rolls_back_cursors'):
        raise ValueError('Require accepted translated DirectSound PCM/ABI proof')
    frozen={str(Path(n).resolve()):h for n,h in proof['source_sha256'].items()}
    sdk=a.sdk.resolve();nx=a.libnx.resolve();identity=sdk_identity(sdk)
    if identity!=proof['sdk_sha256']:raise ValueError('SDK changed')
    sources=[pc/'native'/n for n in ('dsound8.c','pcm_queue.c','audio_horizon.c')]
    inputs=[pc/'native/dsound_listener.inc',*sources,*sorted((pc/'native').glob('*.h')),Path(__file__).resolve(),pc/'tools/build_cpu_runtime.py',pc/'tools/run_aot_analysis.py',a.fixture_validation.resolve(),a.cc.resolve(),a.ld.resolve(),a.nm.resolve(),nx/'lib/libnx.a',*sorted((nx/'include').rglob('*.h'))]
    for p in inputs:
        n=str(p.resolve());h=sha(p)
        if n in frozen and frozen[n]!=h:raise ValueError('Proof source changed: '+n)
        frozen[n]=h
    for n,h in frozen.items():
        if sha(n)!=h:raise ValueError('Proof source changed: '+n)
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
    linked=out/'dsound-audio-arm64.o';run([a.ld.resolve(),'-r',*objects,'-o',linked],'link.log')
    defined=run([a.nm.resolve(),'--defined-only','-g',linked],'defined.log')
    names={line.split()[-1] for line in defined.splitlines() if line.split()}
    required={'fnv_dsound8_create','fnv_dsound8_pump','fnv_dsound8_produce','fnv_pcm_write','fnv_pcm_read','fnv_horizon_audio_open_source'}
    if not required<=names:raise ValueError('Missing linked audio path')
    undefined=run([a.nm.resolve(),'-u',linked],'undefined.log')
    pending={line.split()[-1] for line in undefined.splitlines() if line.split()}
    if required&pending:raise ValueError('Unresolved internal audio path')
    definitions=run([a.nm.resolve(),'--defined-only','-g',nx/'lib/libnx.a'],'libnx.log')
    nx_symbols={line.split()[-1] for line in definitions.splitlines() if len(line.split())>=3}
    required_nx={'audoutInitialize','audoutStartAudioOut','audoutWaitPlayFinish','audoutAppendAudioOutBuffer'}
    if not required_nx<=nx_symbols:raise ValueError('Required audout exports unavailable')
    if sdk_identity(sdk)!=identity or any(sha(n)!=h for n,h in frozen.items()):raise ValueError('Input changed')
    result=dict(status='PASS',source_sha256=frozen,sdk_sha256=identity,compiler_flags=flags,compile_units=3,
        linked_sha256=sha(linked),linked_audio_exports=sorted(required),undefined=sorted(pending),
        switch_hardware_tested=False,engine_started=False,new_nro=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('source_sha256','undefined')},indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','libnx','cc','ld','nm','fixture-validation','output'):p.add_argument('--'+name,type=Path,required=True)
    build(p.parse_args())

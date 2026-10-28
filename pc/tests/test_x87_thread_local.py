"""Check private SoftFloat state, concurrency and the actual Horizon TLS ABI."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True);pc=Path(__file__).resolve().parents[1]
    native=json.loads(a.native.read_text());arm=json.loads(a.arm.read_text())
    for proof in (native,arm):
        if proof.get('status')!='COMPILED' or not proof.get('thread_local_state'):raise ValueError('Require whole-core private-state build')
        if not {'-DTHREAD_LOCAL=_Thread_local','-DFNV_X87_THREAD_LOCAL_STATE=1'}<=set(proof['flags']):raise ValueError('Missing private-state flags')
        for n,h in proof['source_sha256'].items():
            if sha(n)!=h:raise ValueError('Changed source: '+n)
        for r in proof['objects']:
            if sha(r['object'])!=r['sha256']:raise ValueError('Changed object')
    def command(args,name,expected=0):
        r=subprocess.run(list(map(str,args)),capture_output=True,text=True)
        (out/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode!=expected:raise RuntimeError(name+': '+r.stdout+r.stderr)
        return r.stdout
    def response(rows,name):
        p=out/name;p.write_text('\n'.join('"'+str(Path(r['object'])).replace('\\','/')+'"' for r in rows)+'\n');return p
    source=pc/'tests/x87_thread_local_host.c';exe=out/'stress.exe'
    command([*native['compiler'],*native['flags'],source,'@'+str(response(native['objects'],'native.rsp')),'-o',exe],'native-link')
    result=command([exe],'stress')
    combined=out/'core.arm64.o';rsp=response(arm['objects'],'arm.rsp')
    command([a.ld,'-r','@'+str(rsp),'-o',combined],'arm-link')
    symbols=command([a.readelf,'-sW',combined],'arm-symbols')
    names=('softfloat_roundingMode','softfloat_exceptionFlags','extF80_roundingPrecision','softfloat_detectTininess')
    for name in names:
        rows=[line.split() for line in symbols.splitlines() if line.split() and line.split()[-1]==name]
        if len(rows)!=1 or rows[0][3]!='TLS':raise ValueError('SoftFloat state is not TLS: '+name)
    wrapper=next(Path(r['object']) for r in arm['objects'] if Path(r['object']).name=='x87_extended.o')
    text=command([a.readelf,'-sW',wrapper],'wrapper-symbols')
    if 'guard' in text or '__libc_lock_acquire' in text:raise ValueError('Private core still takes global lock')
    # An original global-state object must not link into a TLS core.
    original=json.loads(a.global_core.read_text())
    legacy=next(r for r in original['objects'] if Path(r['object']).name=='s_roundPackToExtF80.o')
    if sha(legacy['object'])!=legacy['sha256']:raise ValueError('Changed negative-control object')
    mixed=[dict(r,object=legacy['object']) if Path(r['object']).name=='s_roundPackToExtF80.o' else r for r in arm['objects']]
    bad=out/'mixed.o';r=subprocess.run([str(a.ld),'-r','@'+str(response(mixed,'mixed.rsp')),'-o',str(bad)],capture_output=True,text=True)
    (out/'mixed.log').write_text(r.stdout+r.stderr)
    if not r.returncode or 'TLS' not in r.stderr:raise ValueError('Mixed global/TLS objects were not rejected')
    report=dict(status='PASS',cases=200000,result=result.strip(),distinct_thread_state=True,horizon_state_symbols_tls=4,
        global_lock_removed=True,mixed_global_tls_rejected=True,switch_hardware_tested=False,
        input_sha256={str(p.resolve()):sha(p) for p in (a.native,a.arm,a.global_core,source,Path(__file__))})
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('native','arm','global-core','ld','readelf','output'):p.add_argument('--'+k,type=Path,required=True)
    run(p.parse_args())

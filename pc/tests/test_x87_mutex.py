"""Stress the Horizon lock branch with a host shim; inspect real ARM64 ABI.

The host shim proves exclusion/state isolation, not Switch priority inheritance.
No game inputs are used. Existing SoftFloat objects must match their receipt.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def run(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True);pc=Path(__file__).resolve().parents[1]
    proof=json.loads(a.core.read_text());root=a.softfloat.resolve()
    for name,digest in proof['source_sha256'].items():
        p=Path(name)
        if p.is_relative_to(root) and sha(p)!=digest:raise ValueError('Changed SoftFloat source: '+str(p))
    objects=[]
    for row in proof['objects']:
        obj=Path(row['object'])
        if sha(obj)!=row['sha256']:raise ValueError('Changed input object: '+str(obj))
        if Path(row['source']).stem not in ('x87_extended','x87_stack','x87_transcendental_candidate','x87_trig_candidate'):
            objects.append(obj)
    frozen={str(p):sha(p) for p in [pc/'native/x87_extended.c',pc/'native/x87_extended.h',Path(__file__),pc/'tests/x87_mutex_host.c',a.core]}
    shim=out/'shim/sys';shim.mkdir(parents=True)
    (shim/'lock.h').write_text('#include <windows.h>\ntypedef SRWLOCK _LOCK_T;\n#define __LOCK_INITIALIZER SRWLOCK_INIT\nvoid __libc_lock_acquire(_LOCK_T*);\nvoid __libc_lock_release(_LOCK_T*);\n')
    def command(args,name):
        r=subprocess.run(list(map(str,args)),capture_output=True,text=True)
        (out/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+': '+r.stdout+r.stderr)
        return r.stdout
    includes=['-DSOFTFLOAT_FAST_INT64','-DINLINE_LEVEL=5','-DSOFTFLOAT_FAST_DIV64TO32','-DSOFTFLOAT_FAST_DIV32TO16',
              '-I'+str(root/'build/Linux-x86_64-GCC'),'-I'+str(root/'source/8086'),'-I'+str(root/'source/include'),'-I'+str(pc/'native')]
    wrapper=out/'x87_extended.o'
    command([a.cc,'-std=c11','-O2','-D__SWITCH__','-I'+str(out/'shim'),*includes,'-c',pc/'native/x87_extended.c','-o',wrapper],'host-wrapper')
    rsp=out/'objects.rsp';rsp.write_text('\n'.join('"'+p.as_posix()+'"' for p in [wrapper,*objects]))
    exe=out/'stress.exe'
    command([a.cc,'-O2','-I'+str(out/'shim'),'-I'+str(pc/'native'),pc/'tests/x87_mutex_host.c','@'+str(rsp),'-o',exe],'host-link')
    result=command([exe],'stress')
    # Prove that the stress harness detects a missing exclusion boundary.
    negative=out/'no_mutex.c'
    negative.write_text((pc/'tests/x87_mutex_host.c').read_text().replace('AcquireSRWLockExclusive(lock);','(void)lock;').replace('ReleaseSRWLockExclusive(lock);','(void)lock;'))
    bad=out/'no_mutex.exe'
    command([a.cc,'-O2','-I'+str(out/'shim'),'-I'+str(pc/'native'),negative,'@'+str(rsp),'-o',bad],'negative-link')
    rejected=subprocess.run([str(bad)],capture_output=True,text=True)
    (out/'negative.log').write_text(rejected.stdout+rejected.stderr)
    if rejected.returncode!=1:raise ValueError('Negative control failed to detect missing mutex')
    from sys import path
    path.insert(0,str(pc/'tools'))
    from build_cpu_runtime import SWITCH_FLAGS
    arm=out/'x87_extended.arm64.o'
    command([a.arm_cc,*SWITCH_FLAGS,*includes,'-c',pc/'native/x87_extended.c','-o',arm],'arm64-wrapper')
    symbols=command([a.nm,'--format=posix',arm],'arm64-symbols')
    if '__libc_lock_acquire U' not in symbols or '__libc_lock_release U' not in symbols:raise ValueError('Missing blocking mutex calls')
    if not any(line.startswith('guard b ') and line.split()[-1]=='4' for line in symbols.splitlines()):raise ValueError('Wrong Horizon lock ABI')
    if any(sha(Path(p))!=h for p,h in frozen.items()):raise ValueError('Sources changed during test')
    report=dict(status='PASS',cases=80000,host_result=result.strip(),missing_mutex_rejected=True,horizon_abi_compiled=True,horizon_scheduling_tested=False,source_sha256=frozen,arm_object_sha256=sha(arm))
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for k in ('core','softfloat','cc','arm-cc','nm','output'):p.add_argument('--'+k,type=Path,required=True)
    run(p.parse_args())

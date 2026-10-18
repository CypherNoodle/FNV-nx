"""Compile/link HID/XInput and PCM/audout for Switch; not an NRO or game boot."""
import argparse
import json
from pathlib import Path
import re
import subprocess
from build_cpu_runtime import SWITCH_FLAGS
from run_aot_analysis import sdk_identity,sha

CPU_SYMBOLS={'fnv_cpu_argument_u32','fnv_cpu_current','fnv_cpu_register_native',
    'fnv_cpu_return_u32','fnv_cpu_try_memory','fnv_cpu_unregister_native',
    'fnv_windows_current_memory','fnv_windows_runtime','fnv_vm_binding','fnv_vm_access','recomp_unimpl'}
C_SYMBOLS={'calloc','free','memcmp','memcpy','memset','memalign'}


def build(a):
    pc=Path(__file__).resolve().parents[1];proof=json.loads(a.fixture_validation.read_text())
    if a.output.exists():raise ValueError('Use a fresh output directory')
    if proof['status']!='PASS' or proof['translated_input_states']!=5000 or proof['concurrent_pcm_frames']!=80000:
        raise ValueError('Require accepted translated ordinal XInput/PCM fixture')
    for name,h in proof['source_sha256'].items():
        if sha(Path(name))!=h:raise ValueError('Fixture source changed: '+name)
    sdk=a.sdk.resolve();nx=a.libnx.resolve();sdk_hash=sdk_identity(sdk)
    if sdk_hash!=proof['sdk_sha256']:raise ValueError('SDK identity differs from fixture')
    sources=[pc/'native'/n for n in ('xinput.c','pcm_queue.c','input_horizon.c','audio_horizon.c')]
    frozen={str(p.resolve()):sha(p) for p in [*sources,*sorted((pc/'native').glob('*.h')),Path(__file__),
        pc/'tools/build_cpu_runtime.py',pc/'tools/run_aot_analysis.py',a.fixture_validation.resolve(),
        pc/'native/CMakeLists.txt',a.cc.resolve(),a.ld.resolve(),a.nm.resolve(),nx/'lib/libnx.a',
        *sorted((nx/'include').rglob('*.h')),
        *[p for d in (sdk/'templates/runtime',sdk/'src/kernel') for p in sorted(d.rglob('*.h'))]]}
    out=a.output.resolve();out.mkdir(parents=True)
    def command(args,name):
        r=subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=120)
        (out/name).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+' failed: '+r.stderr)
        return r.stdout
    includes=['-I'+str(d) for d in (pc/'native',nx/'include',sdk/'templates/runtime',sdk/'src/kernel')]
    objects=[]
    for source in sources:
        obj=out/(source.stem+'.o');objects.append(obj)
        command([a.cc.resolve(),*SWITCH_FLAGS,'-Wall','-Wextra','-Werror',*includes,
            '-include',pc/'native/fnv_sdk_runtime.h','-c',source,'-o',obj],source.stem+'.log')
    linked=out/'fnv-media-relocatable.o';command([a.ld.resolve(),'-r',*objects,'-o',linked],'link.log')
    undefined=set(command([a.nm.resolve(),'-u',linked],'undefined.log').split())-{'U','w'}
    definitions=command([a.nm.resolve(),'--defined-only','-g',nx/'lib/libnx.a'],'libnx-symbols.log')
    symbols={line.split()[-1] for line in definitions.splitlines() if len(line.split())>=3 and re.fullmatch('[0-9a-fA-F]+',line.split()[-3])}
    missing=sorted(undefined-CPU_SYMBOLS-C_SYMBOLS-symbols)
    if missing:raise ValueError('Unexpected platform symbols: '+','.join(missing))
    if sdk_identity(sdk)!=sdk_hash:raise ValueError('SDK changed during compile')
    for name,h in frozen.items():
        if sha(Path(name))!=h:raise ValueError('Input changed: '+name)
    result=dict(status='PASS',compile_units=4,source_sha256=frozen,sdk_sha256=sdk_hash,
        compiler_flags=SWITCH_FLAGS,object_sha256=sha(linked),undefined_cpu=sorted(undefined&CPU_SYMBOLS),
        undefined_c=sorted(undefined&C_SYMBOLS),libnx_definitions=sorted(undefined&symbols),
        switch_hardware_tested=False,native_engine_started=False,new_nro=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','libnx','cc','ld','nm','fixture-validation','output'):p.add_argument('--'+name,type=Path,required=True)
    build(p.parse_args())

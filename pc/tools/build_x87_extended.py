"""Build the standalone extended arithmetic core against official SoftFloat 3e.

No download, SDK edits, game inputs or existing-output reuse is performed.
The caller supplies an extracted, licensed SoftFloat source distribution.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import subprocess


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def build(a):
    if a.output.exists():
        raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True)
    root=a.softfloat.resolve();pc=Path(__file__).resolve().parents[1]
    make=(root/'build/Linux-x86_64-GCC/Makefile').read_text()
    names=list(dict.fromkeys(re.findall(r'\b([A-Za-z0-9_]+)\$\(OBJ\)',make)))
    selected={'extF80_add','extF80_sub','extF80_mul','extF80_div','extF80_sqrt','softfloat_state',
              'f32_to_extF80','f64_to_extF80','extF80_to_f32','extF80_to_f64'}
    selected.update(('extF80_eq','extF80_eq_signaling','extF80_lt'))
    selected.update(('i64_to_extF80','extF80_to_i32','extF80_to_i64','extF80_roundToInt'))
    sources=[]
    for name in names:
        if not (name.startswith('s_') or name.startswith('softfloat_') or name in selected):continue
        options=[root/'source/8086'/f'{name}.c',root/'source'/f'{name}.c']
        source=next((p for p in options if p.is_file()),None)
        if source is None:raise ValueError('Missing source '+name)
        sources.append(source)
    if not selected.issubset({p.stem for p in sources}):raise ValueError('Incomplete arithmetic selection')
    sources.append(pc/'native/x87_extended.c')
    sources.append(pc/'native/x87_stack.c')
    platform=root/'build/Linux-x86_64-GCC/platform.h'
    (out/'platform.h').write_bytes(platform.read_bytes())
    dependencies=[Path(__file__).resolve(),Path(a.cc[0]).resolve(),pc/'native/x87_extended.h',pc/'native/x87_stack.h',platform,root/'COPYING.txt',
                  root/'build/Linux-x86_64-GCC/Makefile',*sources,
                  *sorted((root/'source/include').glob('*.h')),*sorted((root/'source/8086').glob('*.h'))]
    frozen={str(p):sha(p) for p in dependencies}
    base_flags=['-std=c11','-O2','-ffunction-sections','-fdata-sections','-fno-strict-aliasing']
    if a.switch_abi:
        from build_cpu_runtime import SWITCH_FLAGS
        base_flags=list(SWITCH_FLAGS)
        for name in ('build_cpu_runtime.py','aot_code_data.py','run_aot_analysis.py'):
            dependency=Path(__file__).with_name(name).resolve();frozen[str(dependency)]=sha(dependency)
    flags=[*base_flags,
           '-DSOFTFLOAT_FAST_INT64','-DINLINE_LEVEL=5','-DSOFTFLOAT_FAST_DIV64TO32','-DSOFTFLOAT_FAST_DIV32TO16',
           '-Werror=implicit-function-declaration','-I'+str(out),'-I'+str(root/'source/8086'),
           '-I'+str(root/'source/include'),'-I'+str(pc/'native')]
    def compile_one(source):
        obj=out/(source.stem+'.o')
        r=subprocess.run([*a.cc,*flags,'-c',str(source),'-o',str(obj)],capture_output=True,text=True)
        (out/(source.stem+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(source.name+': '+r.stderr[-2000:])
        return dict(source=str(source),object=str(obj),sha256=sha(obj))
    with ThreadPoolExecutor(max_workers=4) as pool:
        compiled=list(pool.map(compile_one,sources))
    assert all(sha(Path(p))==h for p,h in frozen.items())
    result=dict(status='COMPILED',objects=compiled,source_sha256=frozen,flags=flags,compiler=a.cc,
                switch_abi=a.switch_abi,runtime_integrated=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    (out/'objects.rsp').write_text('\n'.join('"'+i['object'].replace('\\','/')+'"' for i in compiled)+'\n')
    print('COMPILED',len(compiled),'extended-core objects; not linked into game runtime')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('softfloat','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    p.add_argument('--switch-abi',action='store_true')
    build(p.parse_args())

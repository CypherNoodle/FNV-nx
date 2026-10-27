"""Cross-compile the combined native runtime; no game code, executable or NRO.

The CPU proof selects an ABI only. Its generated objects are never reused here.
Archive symbol coverage is not a final executable link or execution evidence.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import subprocess
from build_cpu_runtime import SWITCH_FLAGS
from run_aot_analysis import sdk_identity, sha
from x87_models import model_flags


def build(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve()
    if out.exists():raise ValueError('Use a fresh output directory')
    if not 1<=a.jobs<=8:raise ValueError('Use between one and eight compiler jobs')
    cpu=json.loads(a.cpu_abi_validation.read_text());core=json.loads(a.x87_core_validation.read_text())
    flags=[*SWITCH_FLAGS,'-DFNV_CPU_EXTENDED_X87=1',*model_flags(core['transcendental_model'])]
    if cpu.get('status')!='PASS' or cpu.get('compiler_flags')!=flags:
        raise ValueError('Require the matching complete CPU ABI proof')
    if (core.get('status')!='COMPILED' or core.get('switch_abi') is not True or
        cpu.get('x87_core_validation_sha256')!=sha(a.x87_core_validation)):
        raise ValueError('Require the exact Switch x87 core selected by that CPU proof')
    if len(core.get('compiler',[]))!=1 or Path(core['compiler'][0]).resolve()!=a.cc.resolve():
        raise ValueError('Core compiler differs from the selected compiler')
    for name,digest in core['source_sha256'].items():
        if sha(Path(name))!=digest:raise ValueError('Changed x87 dependency: '+name)
    sdk=a.sdk.resolve();sdk_hash=sdk_identity(sdk)
    if sdk_hash!=cpu['sdk_sha256']:raise ValueError('SDK differs from the CPU ABI proof')
    nx=a.devkitpro.resolve()/'libnx'
    includes=[pc/'native',nx/'include',sdk/'templates/runtime',sdk/'src/kernel',a.headers.resolve(),
              *[p.resolve() for p in a.include]]
    if any(not p.is_dir() for p in includes):raise ValueError('Missing include directory')
    sources=[p for p in sorted((pc/'native').glob('*.c')) if not p.name.startswith('x87_')]
    core_objects=[]
    for row in core['objects']:
        p=Path(row['object']).resolve()
        if p in core_objects or sha(p)!=row['sha256']:raise ValueError('Changed or duplicate core object')
        core_objects.append(p)
    archives=[nx/'lib/libnx.a',*[p.resolve() for p in a.archive]]
    for name in ('libc.a','libm.a','libsysbase.a','libgcc.a'):
        r=subprocess.run([str(a.cc.resolve()),'-print-file-name='+name],capture_output=True,text=True,check=True)
        archives.append(Path(r.stdout.strip()).resolve())
    archives=list(dict.fromkeys(archives))
    if any(not p.is_file() for p in archives):raise ValueError('Missing external archive')
    files=[*sources,*core_objects,*archives,a.cpu_abi_validation.resolve(),a.x87_core_validation.resolve(),
           Path(__file__).resolve(),pc/'tools/build_cpu_runtime.py',pc/'tools/run_aot_analysis.py',
           pc/'tools/x87_models.py',a.cc.resolve(),a.ld.resolve(),a.nm.resolve(),a.readelf.resolve(),
           *sorted((pc/'native').glob('*.inc'))]
    for directory in includes:files.extend(sorted(directory.rglob('*.h')))
    frozen={str(p):sha(p) for p in files}
    out.mkdir(parents=True)

    def run(command,name):
        r=subprocess.run([str(x) for x in command],capture_output=True,text=True,timeout=120)
        (out/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+' failed; see its log')
        return r.stdout

    def compile_unit(source):
        obj=out/(source.stem+'.o')
        command=[a.cc.resolve(),*flags,*['-I'+str(p) for p in includes],
                 '-include',pc/'native/fnv_sdk_runtime.h','-c',source,'-o',obj]
        run(command,source.stem)
        return dict(source=str(source),object=str(obj),sha256=sha(obj),command=list(map(str,command)))

    with ThreadPoolExecutor(max_workers=a.jobs) as pool:compiled=list(pool.map(compile_unit,sources))
    linked=out/'fnv-engine-runtime.o';objects=[Path(r['object']) for r in compiled]+core_objects
    arguments=['-r',*map(str,objects),'-o',str(linked)]
    if any('"' in p or '\n' in p or '\r' in p for p in arguments):raise ValueError('Unsupported response path')
    response=out/'runtime-link.rsp';response.write_text('\n'.join('"'+p.replace('\\','/')+'"' for p in arguments)+'\n')
    run([a.ld.resolve(),'@'+str(response)],'runtime-link')
    header=run([a.readelf.resolve(),'-h',linked],'runtime-header')
    if not re.search(r'Machine:\s+AArch64\b',header):raise ValueError('Wrong runtime architecture')
    undefined_text=run([a.nm.resolve(),'-u','--format=posix',linked],'runtime-undefined')
    undefined={m.group(1) for line in undefined_text.splitlines()
               if (m:=re.match(r'^(\S+)\s+U\b',line))}
    available=set();archive_reports=[]
    for i,archive in enumerate(archives):
        header=run([a.readelf.resolve(),'-h',archive],'archive-header-'+str(i))
        machines=re.findall(r'Machine:\s+([^\r\n]+)',header)
        if not machines or any(m.strip()!='AArch64' for m in machines):
            raise ValueError('Non-AArch64 archive: '+str(archive))
        text=run([a.nm.resolve(),'--defined-only','--extern-only','--format=posix',archive],'archive-symbols-'+str(i))
        definitions={m.group(1) for line in text.splitlines()
                     if (m:=re.match(r'^(\S+)\s+[A-TV-Zatv-z]\s+[0-9a-fA-F]+\b',line))}
        available.update(definitions)
        archive_reports.append(dict(path=str(archive),sha256=sha(archive),members=len(machines),definitions=len(definitions)))
    engine=undefined&{'recomp_lookup'};missing=sorted(undefined-available-engine)
    if sdk_identity(sdk)!=sdk_hash or any(sha(Path(p))!=digest for p,digest in frozen.items()):
        raise ValueError('Build inputs changed')
    result=dict(status='PASS' if not missing else 'SYMBOL_COVERAGE_FAILURE',
                native_units=len(compiled),x87_objects=len(core_objects),compiled=compiled,
                compiler_flags=flags,source_sha256=frozen,sdk_sha256=sdk_hash,
                runtime_object=str(linked),runtime_sha256=sha(linked),runtime_bytes=linked.stat().st_size,
                undefined_symbols=sorted(undefined),archive_covered=sorted(undefined&available),
                engine_symbols=sorted(engine),missing_symbols=missing,archives=archive_reports,
                archive_symbol_coverage_only=True,generated_cpu_objects_reused=False,
                full_engine_linked=False,nro_created=False,game_started=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','devkitpro','headers','cpu-abi-validation','x87-core-validation','cc','ld','nm','readelf','output'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--include',type=Path,action='append',default=[])
    p.add_argument('--archive',type=Path,action='append',default=[])
    p.add_argument('--jobs',type=int,default=3)
    result=build(p.parse_args())
    print(json.dumps({k:result[k] for k in ('status','native_units','x87_objects','runtime_bytes','engine_symbols','missing_symbols','full_engine_linked','nro_created')}))
    if result['status']!='PASS':raise SystemExit(1)

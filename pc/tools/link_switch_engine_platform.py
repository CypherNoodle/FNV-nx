"""Final ELF closure check for complete private CPU/runtime and platform archives.

No engine execution, NRO creation, shader admission or hardware claim. The own
main deliberately does not call guest code. All public CPU/runtime definitions
are linker roots: a tiny successful ELF must not hide unused provider symbols.
Section GC still removes unreachable Unix/Rust code from the Mesa archive.
"""
import argparse
import hashlib
import json
import os
import re
import struct
from pathlib import Path
import subprocess

def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1<<20),b''):h.update(block)
    return h.hexdigest()

def build(a):
    out=a.output.resolve()
    if out.exists():raise ValueError('Use a fresh output directory')
    pc=Path(__file__).resolve().parents[1]
    cpu=json.loads(a.cpu_validation.read_text())
    runtime=json.loads(a.runtime_validation.read_text())
    mesa=json.loads(a.mesa_validation.read_text())
    if cpu.get('status')!='PARTIAL_LINKED' or cpu.get('missing_symbols'):
        raise ValueError('Require the complete accepted CPU/runtime partial link')
    if cpu.get('generated_units')!=359 or not 2<=cpu.get('supplementary_entries',0)<=64:
        raise ValueError('Unexpected complete-generation inventory')
    if runtime.get('status')!='PASS' or runtime.get('native_units')!=51:
        raise ValueError('Require the accepted native runtime archive inventory')
    if Path(cpu['inputs']['runtime_validation']).resolve()!=a.runtime_validation.resolve():
        raise ValueError('CPU and native runtime proofs do not match')
    if mesa.get('status')!='PASS' or not mesa.get('static_vulkan'):
        raise ValueError('Require the accepted static Vulkan platform proof')
    frozen={}
    def pin(path,digest=None):
        path=Path(path).resolve();value=sha(path)
        if digest is not None and digest!=value:raise ValueError('Changed input: '+str(path))
        if str(path) in frozen and frozen[str(path)]!=value:raise ValueError('Conflicting input proof')
        frozen[str(path)]=value
        return path
    # Complete source/object identities from the accepted partial link.
    for name,digest in cpu['source_sha256'].items():pin(name,digest)
    partial=pin(cpu['runtime_object'],cpu['runtime_sha256'])
    archives=[pin(r['path'],r['sha256']) for r in runtime['archives']]
    mesa_candidates=[(p,d) for p,d in mesa['source_sha256'].items() if Path(p).name=='libvulkan.a']
    if len(mesa_candidates)!=1:raise ValueError('Ambiguous Mesa archive')
    mesa_archive=pin(*mesa_candidates[0])
    cc=pin(a.cc);nm=pin(a.nm);readelf=pin(a.readelf)
    if runtime['source_sha256'].get(str(cc))!=sha(cc):raise ValueError('Compiler differs from native runtime')
    nx=a.devkitpro.resolve()/'libnx';port=a.devkitpro.resolve()/'portlibs/switch/lib'
    pin(nx/'switch.specs');pin(nx/'switch.ld')
    for path in sorted(port.glob('*.a')):pin(path)
    pin(Path(subprocess.check_output([str(cc),'-print-file-name=libstdc++.a'],text=True).strip()))
    sources=[pc/'switch/engine_link_check.c',pc/'switch/engine_layout.c',pc/'switch/engine_process_plan.c']
    for path in (*sources,pc/'switch/engine_layout.h',pc/'switch/engine_process_plan.h',
                 *sorted((pc/'native').glob('*.h')),Path(__file__),a.cpu_validation,a.runtime_validation,a.mesa_validation):pin(path)
    out.mkdir(parents=True)
    env=os.environ.copy();env['DEVKITPRO']=a.devkitpro.resolve().as_posix()
    commands=[]
    def run(command,name):
        command=list(map(str,command));commands.append(command)
        (out/'progress.json').write_text(json.dumps(dict(phase=name,commands=commands),indent=2)+'\n')
        result=subprocess.run(command,env=env,capture_output=True,text=True)
        (out/(name+'.log')).write_text(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(name+': '+(result.stdout+result.stderr)[-6000:])
        return result.stdout
    flags=['-std=gnu11','-O1','-Wall','-Wextra','-Werror','-march=armv8-a+crc+crypto',
           '-mtune=cortex-a57','-mtp=soft','-ftls-model=local-exec','-fPIE','-D__SWITCH__',
           '-DFNV_CPU_EXTENDED_X87=1','-I'+str(pc/'native'),
           '-I'+str(pc/'switch'),'-I'+str(a.headers.resolve()),'-I'+str(nx/'include')]
    objects=[]
    for index,source in enumerate(sources):
        obj=out/f'entry-{index}.o';run([cc,*flags,'-c',source,'-o',obj],f'compile-{index}');objects.append(obj)
    definitions=run([nm,'--defined-only','--extern-only','--format=posix',partial],'cpu-symbols')
    roots=sorted({p[0] for line in definitions.splitlines() if len(p:=line.split())>=2 and p[1].upper() in {'T','D','B','R','S','V','W'}})
    if not roots or any(not re.fullmatch(r'[A-Za-z_.$][A-Za-z_0-9.$]*',name) for name in roots):
        raise ValueError('Invalid CPU symbol inventory')
    root_script=out/'cpu-roots.ld'
    root_script.write_text('/* Private generated symbol inventory, not public source. */\nEXTERN(\n'+'\n'.join(roots)+'\n)\n')
    # Toolchain archives are supplied by the compiler's normal specs. Media
    # and libnx are passed explicitly, inside the cyclic archive group.
    explicit=[p for p in archives if p.name not in ('libc.a','libm.a','libsysbase.a','libgcc.a')]
    elf=out/'fnv-engine-platform-link.elf'
    run([cc,'-specs='+str(nx/'switch.specs'),'-march=armv8-a+crc+crypto','-mtune=cortex-a57',
         '-mtp=soft','-fPIE','-Wl,--gc-sections','-Wl,-T,'+str(root_script),'-Wl,-Map='+str(out/'link.map'),
         '-o',elf,*objects,partial,'-L'+str(nx/'lib'),'-L'+str(port),'-Wl,--start-group',
         *explicit,mesa_archive,'-lstdc++','-lexpat','-lzstd','-lz','-lm','-Wl,--end-group'],'link')
    undefined=run([nm,'--undefined-only','--format=posix',elf],'undefined')
    if any(len(p:=line.split())>=2 and p[1]=='U' for line in undefined.splitlines()):
        raise ValueError('Final ELF has unresolved strong symbols')
    headers=run([readelf,'-h','-l','-d',elf],'elf-headers')
    if 'AArch64' not in headers or 'DYN' not in headers:raise ValueError('Not a Horizon AArch64 PIE ELF')
    if '(NEEDED)' in headers:raise ValueError('Unexpected dynamic library dependency')
    symbols=run([nm,'--defined-only','--extern-only','--format=posix',elf],'symbols')
    names={parts[0] for line in symbols.splitlines() if len(parts:=line.split())>=2}
    if set(roots)-names:raise ValueError('Public CPU/runtime definitions were discarded')
    required={'recomp_lookup','__wrap_recomp_lookup','sub_00C0CD80','sub_00ECC4DB',
              'vkGetInstanceProcAddr','fnv_horizon_audio_open_source','fnv_engine_layout_create',
              'fnv_engine_process_plan_create'}
    if required-names:raise ValueError('Dropped required link anchors: '+str(sorted(required-names)))
    # Inspect actual loadable memory, not debug/symbol-table file size. Never
    # accept a writable executable segment as a convenient linker workaround.
    with elf.open('rb') as stream:
        header=stream.read(64)
        if header[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',header,18)[0]!=183:
            raise ValueError('Invalid ELF64 little-endian AArch64 header')
        offset=struct.unpack_from('<Q',header,32)[0]
        entry_bytes,count=struct.unpack_from('<HH',header,54)
        if entry_bytes!=56 or not 1<=count<=32:raise ValueError('Unexpected ELF program headers')
        segments=[]
        for index in range(count):
            stream.seek(offset+index*entry_bytes)
            kind,access,file_offset,address,_,file_bytes,memory_bytes,alignment=struct.unpack('<IIQQQQQQ',stream.read(56))
            if kind!=1:continue
            if access&3==3 or file_bytes>memory_bytes or file_offset+file_bytes>elf.stat().st_size:
                raise ValueError('Invalid loadable ELF segment')
            segments.append(dict(address=address,file_bytes=file_bytes,memory_bytes=memory_bytes,flags=access,alignment=alignment))
        if len(segments)!=3 or not any(s['flags']==5 for s in segments):raise ValueError('Unexpected Horizon segment layout')
    mapped_bytes=max(s['address']+s['memory_bytes'] for s in segments)
    for name,digest in frozen.items():
        if sha(name)!=digest:raise ValueError('Input changed during link: '+name)
    result=dict(status='FINAL_LINKED',elf=str(elf),elf_bytes=elf.stat().st_size,elf_sha256=sha(elf),
                source_sha256=frozen,commands=commands,section_gc_disabled=False,
                rooted_cpu_definitions=len(roots),root_script_sha256=sha(root_script),
                load_segments=segments,mapped_bytes=mapped_bytes,
                required_symbols=sorted(required),unresolved_strong_symbols=[],
                dynamic_library_dependencies=[],final_executable_linked=True,
                bootstrap_complete=False,nro_created=False,game_started=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('cpu-validation','runtime-validation','mesa-validation','devkitpro','headers','cc','nm','readelf','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    r=build(parser.parse_args())
    print(json.dumps({k:r[k] for k in ('status','elf_bytes','bootstrap_complete','nro_created','game_started')}))

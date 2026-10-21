"""Package a private, deliberately stopped captured-CRT hardware experiment.

Requires a complete Switch CPU build and capture-consistent desktop boundary
proof. Includes generated game code and captured text data: never publish NROs,
ROMFS, generated profiles or other outputs as reusable project sources.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
from audit_install import pe_info
from audit_startup import audit
from run_aot_analysis import generation_paths,sdk_identity,sha
from initializer_trace import table_entries,check_trace


def crt_x87_inputs(cpu,report,profile_path,core_path,compiler):
    """Carry the CPU build's exact optional x87 ABI and objects into packaging."""
    from build_cpu_runtime import x87_build_inputs
    flags,frozen,objects,digest=x87_build_inputs(report,profile_path,core_path,compiler)
    if digest!=cpu.get('x87_core_validation_sha256'):
        raise ValueError('CRT packaging x87 core differs from the complete CPU build')
    if flags!=cpu['compiler_flags']:
        raise ValueError('CRT packaging flags differ from the complete CPU ABI')
    helper=Path(__file__).with_name('build_cpu_runtime.py').resolve()
    frozen[helper]=sha(helper)
    return frozen,objects,digest


def build(a):
    out=a.output.resolve();pc=Path(__file__).resolve().parents[1];analysis=a.analysis.resolve();sdk=a.sdk.resolve()
    if out.exists():raise ValueError('Use a fresh private output')
    cpu=json.loads(a.cpu_validation.read_text());desktop=json.loads(a.desktop_validation.read_text())
    from startup_memory_profile import require_switch_profile
    require_switch_profile(desktop)
    if desktop.get('extra_entries') or desktop.get('generation_matches_all_bodies') is False:
        raise ValueError('Supplementary host entries are not validated by the complete Switch CPU generation')
    observed=json.loads(a.initializer_validation.read_text())
    table=desktop['initializer_tables'][-1];expected=table['nonzero']
    trace=observed.get('initializer_trace',{})
    checked=check_trace(table,table_entries(a.exe.resolve(),table),trace.get('before'),trace.get('after'))
    if not checked['complete'] or trace.get('validation')!=checked:
        raise ValueError('Initializer slot/target trace is incomplete or mismatched')
    if (observed.get('status')!='OBSERVED' or not observed.get('baseline_control_flow_identical') or
        observed.get('caller_state_changes')!=[] or not observed.get('cursor_monotonic') or
        observed.get('calls_before')!=expected or observed.get('calls_after')!=expected or
        observed.get('source_sha256',{}).get(str(a.desktop_validation.resolve()))!=sha(a.desktop_validation.resolve())):
        raise ValueError('Initializer traversal/return-state validation is incomplete; do not package an early-exit false positive')
    stop=desktop['host']['fault_address'];wrapper=desktop['explicit_wrapper']
    if (cpu['status']!='PASS' or cpu['partial_link']['unexpected_symbols'] or
        desktop['status']!='EXPERIMENT_STOPPED' or desktop['host']['text']!='Unselected startup target' or
        desktop['explicit_stops']!=[stop] or not desktop.get('text_data') or
        desktop['generation']['generation_report_sha256']!=cpu['generation_report_sha256'] or not wrapper):
        raise ValueError('Require complete CPU build and captured-data proof at the deliberate boundary')
    identity=audit(a.exe.resolve(),analysis);profile=json.loads((analysis/'input-profile.json').read_text())
    if sdk_identity(sdk)!=cpu['sdk_sha256'] or desktop['text_data']['sha256']!=profile['text_sha256']:
        raise ValueError('SDK/capture identity mismatch')
    paths=generation_paths(analysis,a.generation_name)
    if sha(paths['report'])!=cpu['generation_report_sha256']:raise ValueError('Wrong generation')
    from startup_host_x87 import validate_policy
    validate_policy(desktop,json.loads(paths['report'].read_text()))
    extra_dependencies,x87_objects,x87_digest=crt_x87_inputs(cpu,json.loads(paths['report'].read_text()),
        getattr(a,'x87_profile',None),getattr(a,'x87_core_validation',None),a.cc)
    frozen={Path(p):h for proof in (cpu,desktop,observed) for p,h in proof['source_sha256'].items()}
    for p,h in extra_dependencies.items():
        if p in frozen and frozen[p]!=h:raise ValueError('CPU/CRT x87 source disagreement')
        frozen[p]=h
    frozen[a.initializer_validation.resolve()]=sha(a.initializer_validation.resolve())
    for p in [Path(__file__).resolve(),pc/'tools/initializer_trace.py',pc/'tools/startup_host_x87.py',pc/'tools/startup_memory_profile.py',pc/'switch/crt_probe.c',a.cpu_validation.resolve(),a.desktop_validation.resolve(),
              *[a.cc.resolve(),a.ld.resolve(),a.nm.resolve(),a.objdump.resolve(),a.readelf.resolve(),a.elf2nro.resolve(),a.nacptool.resolve(),a.build_romfs.resolve(),a.icon.resolve()],
              a.devkitpro.resolve()/'libnx/switch.specs',a.devkitpro.resolve()/'libnx/switch.ld',a.devkitpro.resolve()/'libnx/lib/libnx.a',
              *[pc/'native'/n for n in ('pe_tls.c','windows_thread.c','windows_modules.c','guest_memory.c','memory_horizon.c','windows_memory.c')]]:
        frozen[p]=sha(p)
    objects=[]
    for item in cpu['compiled']:
        p=a.cpu_validation.resolve().parent/(Path(item['source']).stem+'.o')
        if item['exit_code'] or sha(p)!=item['object_sha256']:raise ValueError('Changed complete-build object')
        objects.append(p);frozen[p]=sha(p)
    if set(objects).intersection(x87_objects):raise ValueError('Duplicate CPU/x87 link input')
    objects.extend(x87_objects)
    if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Proof inputs changed')
    out.mkdir(parents=True);env=os.environ.copy();env['DEVKITPRO']=a.devkitpro.resolve().as_posix()
    def run(cmd,name):
        r=subprocess.run([str(x) for x in cmd],env=env,capture_output=True,text=True)
        (out/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+': '+(r.stdout+r.stderr)[-7000:])
        return r.stdout
    # The audited CRT caller must reference a separately defined main. Wrapping
    # at the original object link (not after ld -r) redirects that reference.
    crt=wrapper['targets'][-1];crt_symbol=f'sub_{crt:08X}';stop_symbol=f'sub_{stop:08X}'
    unit_names=[p.name for p in paths['generated'].glob('recomp_????.c') if re.search(r'^void '+crt_symbol+r'\(void\)',p.read_text(),re.M)]
    owners=[p for p in objects if p.with_suffix('.c').name in unit_names]
    if len(owners)!=1:raise ValueError('Ambiguous CRT object owner')
    symbols=run([a.nm,'--format=posix',owners[0]],'crt-symbols')
    if not re.search(r'\b'+stop_symbol+r'\s+U\b',symbols) or re.search(r'\b'+stop_symbol+r'\s+[Tt]\b',symbols):
        raise ValueError('CRT stop must be an external object reference')
    reloc=run([a.objdump,'-r',owners[0]],'crt-original-relocations')
    if not re.search(r'R_AARCH64_CALL26\s+'+stop_symbol+r'\b',reloc):raise ValueError('No audited direct stop call relocation')
    stopped=out/'fnv-crt-stopped.o';rsp=out/'cpu-link.rsp'
    args=['-r','--wrap='+stop_symbol,'-o',stopped.as_posix(),*[p.as_posix() for p in objects]]
    if any('"' in x or '\n' in x for x in args):raise ValueError('Unsupported response path')
    rsp.write_text('\n'.join('"'+x+'"' for x in args)+'\n');run([a.ld,'@'+str(rsp)],'stopped-link')
    undef=run([a.nm,'--undefined-only','--format=posix',stopped],'stopped-undefined')
    names={line.split()[0] for line in undef.splitlines() if line.strip()}
    if names!=set(cpu['partial_link']['undefined_symbols'])|{'__wrap_'+stop_symbol}:raise ValueError('Unexpected stopped-link symbols')
    sys.path[:0]=[str(sdk),str(a.python_deps.resolve())]
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    from aot_frame_restore import install
    from aot_simd_bits import install as simd
    from aot_mul_flags import install as multiply
    config.configure_from_xbe(str(analysis/'fnv-analysis.xbe'));simd();multiply();install()
    image=(analysis/'fnv-analysis.xbe').read_bytes();db={int(f['start'],0):dict(f) for f in json.loads((analysis/'disasm/functions.json').read_text())}
    entry=wrapper['address'];end=wrapper['end'];info=dict(start=hex(entry),end=end,size=end-entry,name=f'sub_{entry:08X}',section='.text',called_by=[])
    db[entry]=info;t=FunctionTranslator(image,db,seh_prolog=0,seh_epilog=0);source=t.translate_function(entry,info)
    if not source or any(x in source for x in ('recomp_unimpl(',': unhandled */','translation failed')):raise ValueError('Unsupported wrapper')
    own='#include "fnv_sdk_runtime.h"\n#include "cpu_runtime.h"\n'+''.join(f'void sub_{at:08X}(void);\n' for at in wrapper['targets'])+source
    own+=f'void fnv_crt_native_entry(void){{sub_{entry:08X}();}}\nvoid __wrap_{stop_symbol}(void){{recomp_unimpl("Reached engine entry (diagnostic stop)",0x{stop:X}u);}}\n'
    imports=[dict(dll=d['dll'],**s) for d in pe_info(a.exe)['imports'] for s in d['symbols']]
    for i,item in enumerate(imports):own+=f'static void missing_{i}(void){{recomp_unimpl({json.dumps(item["dll"]+"!"+item.get("name","#"+str(item.get("ordinal",0))))},0);}}\n'
    own+='fnv_cpu_function fnv_crt_missing_imports[]={'+','.join(f'missing_{i}' for i in range(len(imports)))+'};\n'
    (out/'entry.c').write_text(own)
    header=f'#define FNV_CRT_BASE 0x{profile["image_base"]:X}u\n#define FNV_CRT_STOP 0x{stop:X}u\n#define FNV_CRT_IMPORT_COUNT {len(imports)}u\n'
    text=next(s for s in profile['sections'] if s['name']=='.text')
    header+=f'#define FNV_CRT_TEXT_RVA 0x{text["rva"]:X}u\n#define FNV_CRT_TEXT_BYTES {text["virtual_bytes"]}u\n'
    header+=f'#define FNV_CRT_EXE_SHA256 "{profile["executable_sha256"]}"\n#define FNV_CRT_TEXT_SHA256 "{profile["text_sha256"]}"\nextern fnv_cpu_function fnv_crt_missing_imports[FNV_CRT_IMPORT_COUNT];\n'
    (out/'crt_profile.h').write_text(header)
    rom=out/'romfs';rom.mkdir();blob=a.desktop_validation.resolve().parent/'captured-text-data.bin'
    if sha(blob)!=profile['text_sha256']:raise ValueError('Wrong private text blob')
    frozen[blob]=sha(blob);shutil.copyfile(blob,rom/blob.name)
    flags=cpu['compiler_flags'];includes=['-I'+str(p) for p in (out,pc/'native',sdk/'templates/runtime',sdk/'src/kernel',a.devkitpro.resolve()/'libnx/include')]
    added=[]
    for p in [out/'entry.c',pc/'switch/crt_probe.c',*[pc/'native'/n for n in ('pe_tls.c','windows_thread.c','windows_modules.c','guest_memory.c','memory_horizon.c','windows_memory.c')]]:
        generated_warnings=['-Wno-unused-label','-Wno-unused-variable','-Wno-unused-but-set-variable'] if p.parent==out else []
        obj=out/(p.stem+'.o');run([a.cc,*flags,'-Wall','-Wextra','-Werror',*generated_warnings,*includes,'-include',pc/'native/fnv_sdk_runtime.h','-c',p,'-o',obj],p.stem+'-compile');added.append(obj)
    elf=out/'fnv-nx.elf';nro=out/'fnv-nx.nro'
    run([a.cc,'-specs='+str(a.devkitpro/'libnx/switch.specs'),'-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE',
         '-Wl,-Map='+str(out/'fnv-nx.map'),'-o',elf,*added,stopped,'-L'+str(a.devkitpro/'libnx/lib'),'-lnx','-lm'],'final-link')
    strong=[line for line in run([a.nm,'--undefined-only','--format=posix',elf],'final-undefined').splitlines() if len(s:=line.split())>=2 and s[1]=='U']
    if strong:raise ValueError('Undefined final ELF symbols: '+repr(strong))
    headers=run([a.readelf,'-h','-l','-d',elf],'elf-headers')
    if 'AArch64' not in headers or 'DYN' not in headers:raise ValueError('Incorrect ELF architecture/type')
    run([a.nacptool,'--create','FNV PC CRT AOT','FNV-nx','pc-crt-hw01',out/'fnv-nx.nacp'],'nacp')
    run([a.build_romfs,rom,out/'romfs.bin'],'romfs')
    run([a.elf2nro,elf,nro,'--nacp='+str(out/'fnv-nx.nacp'),'--icon='+str(a.icon),'--romfs='+str(out/'romfs.bin')],'nro')
    raw=nro.read_bytes();size=struct.unpack_from('<I',raw,24)[0]
    if raw[16:20]!=b'NRO0' or raw[size:size+4]!=b'ASET':raise ValueError('Invalid NRO asset/header')
    for at in (32,40,48):
        offset,length=struct.unpack_from('<II',raw,at)
        if offset+length>size:raise ValueError('NRO segment out of bounds')
    for at in (8,24,40):
        offset,length=struct.unpack_from('<QQ',raw,size+at)
        if size+offset+length>len(raw):raise ValueError('NRO asset out of bounds')
    rom_offset,rom_bytes=struct.unpack_from('<QQ',raw,size+40)
    if raw[size+rom_offset:size+rom_offset+rom_bytes]!=(out/'romfs.bin').read_bytes():raise ValueError('Embedded private ROMFS mismatch')
    if any(sha(p)!=h for p,h in frozen.items()) or audit(a.exe.resolve(),analysis)!=identity:raise ValueError('Build inputs changed')
    result=dict(status='PASS',build='pc-crt-hw01',nro_sha256=sha(nro),nro_bytes=len(raw),elf_sha256=sha(elf),
                source_sha256={str(p):h for p,h in frozen.items()},cpu_compile_units=cpu['compile_units'],
                x87_core_validation_sha256=x87_digest,x87_core_objects_linked=len(x87_objects),
                stopped_link_sha256=sha(stopped),wrapper_entry=entry,deliberate_stop=stop,desktop_indirect_calls=desktop['host']['call_count'],
                generated_game_code_embedded=True,captured_game_text_embedded=True,engine_frame_enabled=False,
                cold_boot_supported=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--x87-profile',type=Path,help='Exact optional profile from the complete CPU build')
    p.add_argument('--x87-core-validation',type=Path,help='Matching Switch x87 core proof; its objects are linked too')
    for n in ('analysis','exe','sdk','python-deps','cpu-validation','desktop-validation','initializer-validation','devkitpro','cc','ld','nm','objdump','readelf','elf2nro','nacptool','build-romfs','icon','output'):
        p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--generation-name',required=True);build(p.parse_args())

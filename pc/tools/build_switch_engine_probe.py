"""Package the private captured-PC engine diagnostic; never publish its outputs.

Requires the complete platform-link proof, original EXE identity, audited
captured entry, original bytecode catalog and separately prepared shader bundle.
The experimental caps profile is explicit; generic D3D9 conformance is unproven.
"""
import argparse
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
from audit_install import pe_info
from link_switch_engine_platform import sha

def catalog_entries(data):
    if len(data)<12 or len(data)>16<<20:raise ValueError('Invalid shader catalog extent')
    version,count,payload=struct.unpack_from('<III',data)
    if version!=100 or not 1<=count<=8192 or payload!=len(data)-12:raise ValueError('Invalid shader catalog header')
    entries=[];offset=12
    for _ in range(count):
        if offset+260>len(data):raise ValueError('Truncated shader catalog entry')
        name=data[offset:offset+256];end=name.find(b'\0')
        if end<=0 or any(c<32 or c>126 for c in name[:end]):raise ValueError('Invalid shader catalog name')
        size=struct.unpack_from('<I',data,offset+256)[0];offset+=260
        if size<8 or size%4 or offset+size>len(data):raise ValueError('Invalid shader bytecode extent')
        code=data[offset:offset+size];offset+=size;token=struct.unpack_from('<I',code)[0]
        if token>>16 not in (0xfffe,0xffff) or not 1<=((token>>8)&255)<=3 or code[-4:]!=b'\xff\xff\0\0':
            raise ValueError('Invalid shader bytecode envelope')
        entries.append(code)
    if offset!=len(data):raise ValueError('Trailing shader catalog bytes')
    return entries

def bundle_membership(header,entries):
    original=re.findall(r'static const uint32_t bundle_\d+_(?:vs|ps)_code\[\]=\{([^}]+)\};',header)
    if not original:raise ValueError('Missing prepared original shader identities')
    known=set(entries)
    for values in original:
        words=[int(v.strip().rstrip('uU'),0) for v in values.split(',') if v.strip()]
        code=struct.pack('<'+'I'*len(words),*words)
        if code not in known:raise ValueError('Prepared shader absent from original catalog')
    return len(original)

def build(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve()
    if out.exists():raise ValueError('Use a fresh output directory')
    if not re.fullmatch(r'hw\d{2}',a.hardware_revision):raise ValueError('Invalid hardware revision')
    platform=json.loads(a.platform_validation.read_text())
    cpu=json.loads(a.cpu_validation.read_text())
    runtime=json.loads(a.runtime_validation.read_text())
    profile=json.loads(a.profile.read_text());entry=json.loads(a.entry_evidence.read_text())
    bundle=json.loads(a.bundle_validation.read_text())
    if platform.get('status')!='FINAL_LINKED' or platform.get('unresolved_strong_symbols'):
        raise ValueError('Require complete final platform link')
    if platform.get('rooted_cpu_definitions',0)<89000:raise ValueError('Incomplete CPU/runtime closure')
    if cpu.get('status')!='PARTIAL_LINKED' or runtime.get('status')!='PASS':raise ValueError('Require accepted CPU/runtime inputs')
    if entry.get('status')!='AUDITED_DESKTOP_ENTRY_BOUNDARY' or not entry.get('translated_body_control_flow_checked'):
        raise ValueError('Require audited captured-PC entry')
    if entry.get('exe_sha256')!=profile.get('executable_sha256') or entry.get('capture_sha256')!=profile.get('text_sha256'):
        raise ValueError('Entry identity differs from PC profile')
    if bundle.get('status')!='PASS' or bundle.get('programs')!=54:raise ValueError('Require prepared 54-program bundle')
    frozen={}
    def pin(path,digest=None):
        path=Path(path).resolve();value=sha(path)
        if digest is not None and value!=digest:raise ValueError('Changed input: '+str(path))
        if str(path) in frozen and frozen[str(path)]!=value:raise ValueError('Conflicting build evidence')
        frozen[str(path)]=value;return path
    for name,digest in platform['source_sha256'].items():pin(name,digest)
    for name,digest in cpu['source_sha256'].items():pin(name,digest)
    for name,digest in bundle['input_sha256'].items():pin(name,digest)
    pin(a.cpu_validation,platform['source_sha256'].get(str(a.cpu_validation.resolve())))
    pin(a.runtime_validation,platform['source_sha256'].get(str(a.runtime_validation.resolve())))
    pin(a.entry_evidence,cpu['source_sha256'].get(str(a.entry_evidence.resolve())))
    manifest=json.loads(Path(cpu['inputs']['entries']).read_text())
    matches=[e for e in manifest['entries'] if e['address']==entry['start'] and Path(e['boundary_evidence']).resolve()==a.entry_evidence.resolve()]
    if len(matches)!=1:raise ValueError('Entry is not in the linked supplementary resolver')
    partial=pin(cpu['runtime_object'],cpu['runtime_sha256'])
    exe=pin(a.exe,profile['executable_sha256']);capture=pin(a.capture,profile['text_sha256'])
    info=pe_info(exe);text=next(s for s in profile['sections'] if s['name']=='.text')
    if capture.stat().st_size!=text['virtual_bytes']:raise ValueError('Captured text extent mismatch')
    if info['image_base']!=profile['image_base'] or info['image_bytes']!=profile['image_bytes']:
        raise ValueError('PC image bounds mismatch')
    imports=[dict(dll=d['dll'],**s) for d in info['imports'] for s in d['symbols']]
    if len(imports)!=profile['normal_import_slots']:raise ValueError('Original import inventory mismatch')
    catalog=pin(a.catalog);programs=pin(a.bundle_validation.resolve().parent/'startup_program.h',bundle['header_sha256'])
    catalog_codes=catalog_entries(catalog.read_bytes());original_stages=bundle_membership(programs.read_text(),catalog_codes)
    fixed=pin(a.fixed_screen)
    if 'configure_fixed_screen' not in fixed.read_text():raise ValueError('Missing prepared fixed-screen setup')
    sdk=Path(cpu['inputs']['sdk']);nx=a.devkitpro.resolve()/'libnx';port=a.devkitpro.resolve()/'portlibs/switch/lib'
    cc=pin(a.cc);nm=pin(a.nm);readelf=pin(a.readelf)
    if sha(cc)!=runtime['source_sha256'].get(str(cc)):raise ValueError('Compiler differs from native ABI proof')
    elf2nro=pin(a.elf2nro);nacptool=pin(a.nacptool);icon=pin(a.icon)
    mesa=[Path(name) for name in platform['source_sha256'] if Path(name).name=='libvulkan.a']
    if len(mesa)!=1:raise ValueError('Ambiguous static Mesa archive')
    archives=[pin(r['path'],r['sha256']) for r in runtime['archives'] if Path(r['path']).name not in ('libc.a','libm.a','libgcc.a','libsysbase.a')]
    sources=[pc/'switch'/name for name in ('engine_probe.c','engine_layout.c','engine_process_plan.c','engine_movie_io.c')]
    for path in (*sources,*sorted((pc/'switch').glob('engine_*.h')),Path(__file__),a.profile,a.platform_validation,a.bundle_validation):pin(path)
    out.mkdir(parents=True);rom=out/'romfs';rom.mkdir()
    shutil.copyfile(capture,rom/'captured-text-data.bin');shutil.copyfile(catalog,rom/'catalog.sdp')
    shutil.copyfile(programs,out/'prepared_programs.h');shutil.copyfile(fixed,out/'fixed_screen.h')
    (out/'engine_programs.h').write_text('#include "fixed_screen.h"\n#include "prepared_programs.h"\n')
    header=f'#define FNV_ENGINE_BASE {profile["image_base"]}u\n#define FNV_ENGINE_IMAGE_BYTES {profile["image_bytes"]}u\n'
    header+=f'#define FNV_ENGINE_HW_TAG "{a.hardware_revision}"\n'
    header+=f'#define FNV_ENGINE_TEXT_RVA {text["rva"]}u\n#define FNV_ENGINE_TEXT_BYTES {text["virtual_bytes"]}u\n'
    header+=f'#define FNV_ENGINE_ENTRY {entry["start"]}u\n#define FNV_ENGINE_IMPORT_COUNT {len(imports)}u\n'
    header+=f'#define FNV_ENGINE_EXPERIMENTAL_CAPS {int(a.experimental_caps)}\n#define FNV_ENGINE_CATALOG_COUNT {len(catalog_codes)}u\n'
    for name,digest in [('EXE',sha(exe)),('TEXT',sha(capture)),('SDP',sha(catalog))]:header+=f'#define FNV_ENGINE_{name}_SHA256 "{digest}"\n'
    header+='extern fnv_cpu_function fnv_engine_missing_imports[FNV_ENGINE_IMPORT_COUNT];\n'
    (out/'engine_profile.h').write_text(header)
    traps='#include "fnv_sdk_runtime.h"\n#include "cpu_runtime.h"\n'
    for i,item in enumerate(imports):
        label=item['dll']+'!'+item.get('name','#'+str(item.get('ordinal',0)))
        traps+=f'static void missing_{i}(void){{recomp_unimpl({json.dumps(label)},0);}}\n'
    traps+='fnv_cpu_function fnv_engine_missing_imports[]={'+','.join(f'missing_{i}' for i in range(len(imports)))+'};\n'
    trap_source=out/'import_traps.c';trap_source.write_text(traps);sources.append(trap_source)
    env=os.environ.copy();env['DEVKITPRO']=a.devkitpro.resolve().as_posix();commands=[]
    def run(command,name):
        command=list(map(str,command));commands.append(command)
        (out/'progress.json').write_text(json.dumps(dict(phase=name,commands=commands),indent=2)+'\n')
        r=subprocess.run(command,env=env,capture_output=True,text=True)
        (out/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+': '+(r.stdout+r.stderr)[-7000:])
        return r.stdout
    flags=runtime['compiler_flags']
    # The exact native ABI is reused. Include paths copied from the accepted
    # native unit command also cover the unchanged SDK and platform headers.
    includes=[x for x in runtime['compiled'][0]['command'] if x.startswith('-I')]
    includes+=['-I'+str(out),'-I'+str(pc/'switch')]
    objects=[]
    for i,source in enumerate(sources):
        obj=out/(source.stem+'.o');run([cc,*flags,'-Wall','-Wextra','-Werror',*includes,
            '-include',pc/'native/fnv_sdk_runtime.h','-c',source,'-o',obj],f'compile-{i}');objects.append(obj)
    elf=out/'fnv-nx.elf';nro=out/'fnv-nx.nro';nacp=out/'fnv-nx.nacp'
    run([cc,'-specs='+str(nx/'switch.specs'),'-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE',
        '-Wl,-Map='+str(out/'fnv-nx.map'),'-o',elf,*objects,partial,'-L'+str(nx/'lib'),'-L'+str(port),
        '-Wl,--start-group',*archives,mesa[0],'-lstdc++','-lexpat','-lzstd','-lz','-lm','-Wl,--end-group'],'link')
    undefined=run([nm,'--undefined-only','--format=posix',elf],'undefined')
    if any(len(p:=line.split())>=2 and p[1]=='U' for line in undefined.splitlines()):raise ValueError('Unresolved strong engine symbols')
    headers=run([readelf,'-h','-l','-d',elf],'elf-headers')
    if 'AArch64' not in headers or 'DYN' not in headers or '(NEEDED)' in headers:raise ValueError('Invalid Horizon ELF')
    symbols=run([nm,'--defined-only','--extern-only','--format=posix',elf],'symbols')
    for name in ('sub_00ECC4DB','sub_00C0CD80','__wrap_recomp_lookup','fnv_horizon_audio_open_source'):
        if not any(line.split()[0]==name for line in symbols.splitlines() if line.strip()):raise ValueError('Missing engine link anchor: '+name)
    run([nacptool,'--create','FNV PC Engine Experiment','FNV-nx','pc-engine-'+a.hardware_revision,nacp],'nacp')
    run([elf2nro,elf,nro,'--nacp='+str(nacp),'--icon='+str(icon),'--romfsdir='+str(rom)],'nro')
    raw=nro.read_bytes();size=struct.unpack_from('<I',raw,24)[0]
    if raw[16:20]!=b'NRO0' or raw[size:size+4]!=b'ASET':raise ValueError('Invalid NRO/ASET')
    for at in (32,40,48):
        offset,length=struct.unpack_from('<II',raw,at)
        if offset+length>size:raise ValueError('NRO segment out of bounds')
    rom_offset,rom_bytes=struct.unpack_from('<QQ',raw,size+40)
    if not rom_bytes or size+rom_offset+rom_bytes>len(raw):raise ValueError('ROMFS extent outside NRO')
    for name,digest in frozen.items():
        if sha(name)!=digest:raise ValueError('Changed build input: '+name)
    result=dict(status='BUILT',build='pc-engine-'+a.hardware_revision,nro=str(nro),nro_bytes=len(raw),nro_sha256=sha(nro),
        elf_sha256=sha(elf),source_sha256=frozen,commands=commands,experimental_caps=bool(a.experimental_caps),
        volume=0,entry=entry['start'],prepared_programs=54,catalog_entries=len(catalog_codes),original_shader_arrays=original_stages,
        captured_code_embedded=True,generated_game_code_embedded=True,private_output=True,
        guest_engine_execution_enabled=True,cold_boot_supported=False,game_started=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('platform-validation','cpu-validation','runtime-validation','profile','entry-evidence','bundle-validation','exe','capture','catalog','fixed-screen','devkitpro','cc','nm','readelf','elf2nro','nacptool','icon','output'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--experimental-caps',action='store_true')
    p.add_argument('--hardware-revision',default='hw02')
    r=build(p.parse_args());print(json.dumps({k:r[k] for k in ('status','nro_bytes','guest_engine_execution_enabled','cold_boot_supported','switch_hardware_tested')}))

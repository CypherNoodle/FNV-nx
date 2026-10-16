"""Build the own COM/Vulkan/NVK hardware probe, with no game code or assets.

Inputs: accepted synthetic COM fixture and its exact linked shader pair,
separately supplied x86 SDK, pinned Mesa-Switch and devkitPro toolchain.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def build(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve();proof=json.loads(a.fixture_validation.read_text());fixture=a.fixture_validation.resolve().parent
    if out.exists():raise ValueError('Use a fresh output directory')
    if proof['status']!='PASS' or proof['native_engine_started'] or proof['synthetic_functions']!=82:raise ValueError('Require accepted 82-caller own fixture')
    if proof.get('translated_com_present',{}).get('com_wsi_presents')!=4:raise ValueError('Require accepted translated COM Present')
    if proof.get('generated_source_sha256')!=sha(fixture/'generated.c') or proof.get('fixture_sha256')!=sha(fixture/'fixture.exe'):raise ValueError('Synthetic callers differ from tested inputs')
    sdk=a.sdk.resolve();nx=a.devkitpro.resolve()/'libnx';port=a.devkitpro.resolve()/'portlibs/switch/lib'
    sources=[pc/'native'/n for n in ('cpu_runtime.c','pe_image.c','guest_memory.c','memory_horizon.c','d3d9_constants.c','d3d9_com.c','d3d9_vk_backend.c','d3d9_vk_horizon.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_geometry.c','d3d9_vk_state.c')]
    sources+=[fixture/'generated.c',pc/'switch/renderer_probe.c']
    linked=a.linked_shaders.resolve();catalog=fixture/'catalog.bin'
    data=catalog.read_bytes();assert data[:8]==b'FNV9SHD1' and struct.unpack_from('<I',data,8)[0]==2
    assets={};offset=12
    for _ in range(2):
        stage,size=struct.unpack_from('<II',data,offset);offset+=8
        assert stage<2 and size%4==0 and offset+size<=len(data)
        name='probe_ps' if stage else 'probe_vs';assert name not in assets;assets[name]=data[offset:offset+size];offset+=size
    assert offset==len(data)
    assets.update(probe_vs_spv=(linked/'normal.vs.spv').read_bytes(),probe_ps_spv=(linked/'normal.ps.spv').read_bytes())
    for name in ('normal.vs.spv','normal.ps.spv'):
        path=linked/name
        if proof['source_sha256'].get(str(path))!=sha(path):raise ValueError('Shader differs from accepted COM fixture: '+name)
    files=[*sources,catalog,a.fixture_validation.resolve(),Path(__file__).resolve(),*sorted((pc/'native').glob('*.h')),*sorted((pc/'native').glob('*.inc')),
        linked/'normal.vs.spv',linked/'normal.ps.spv',a.mesa.resolve(),a.cc.resolve(),a.nm.resolve(),a.readelf.resolve(),a.elf2nro.resolve(),a.nacptool.resolve(),a.icon.resolve(),nx/'switch.specs',nx/'switch.ld',nx/'lib/libnx.a',
        *[p for d in (sdk/'templates/runtime',sdk/'src/kernel') for p in d.rglob('*.h')]]
    files.extend(sorted(port.glob('*.a')))
    for name in ('libc.a','libm.a','libstdc++.a','libgcc.a','libsysbase.a'):
        p=Path(subprocess.check_output([str(a.cc.resolve()),'-print-file-name='+name],text=True).strip()).resolve()
        if not p.is_file():raise ValueError('Missing toolchain archive: '+name)
        files.append(p)
    frozen={str(p):sha(p) for p in files};out.mkdir(parents=True)
    header=out/'renderer_probe_assets.h'
    header.write_text('#include <stdint.h>\n'+''.join('static const uint32_t '+name+'[]={'+','.join(hex(x) for x in struct.unpack('<'+'I'*(len(raw)//4),raw))+'};\n' for name,raw in assets.items()))
    env=os.environ.copy();env['DEVKITPRO']=a.devkitpro.resolve().as_posix()
    def run(cmd,name):
        r=subprocess.run([str(x) for x in cmd],env=env,capture_output=True,text=True);(out/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+': '+(r.stdout+r.stderr)[-7000:])
        return r.stdout
    flags=['-std=gnu11','-O1','-mearly-ra=none','-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-ftls-model=local-exec','-fPIE','-ffunction-sections','-fdata-sections','-D__SWITCH__','-fno-strict-aliasing','-ffp-contract=off','-Wall','-Wextra','-Werror',
        '-I'+str(pc/'native'),'-I'+str(out),'-I'+str(nx/'include'),'-I'+str(a.headers.resolve()),'-I'+str(sdk/'templates/runtime'),'-I'+str(sdk/'src/kernel'),'-include',str(pc/'native/fnv_sdk_runtime.h')]
    objects=[]
    for i,source in enumerate(sources):
        obj=out/f'unit-{i:02}.o';extra=['-Wno-unused-label','-Wno-unused-variable','-Wno-unused-but-set-variable'] if source.name=='generated.c' else []
        run([a.cc,*flags,*extra,'-c',source,'-o',obj],f'compile-{i:02}');objects.append(obj)
    elf=out/'fnv-nx.elf';nro=out/'fnv-nx.nro';nacp=out/'fnv-nx.nacp'
    run([a.cc,'-specs='+str(nx/'switch.specs'),'-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE','-Wl,-Map='+str(out/'fnv-nx.map'),'-o',elf,*objects,
        '-L'+str(nx/'lib'),'-L'+str(port),'-Wl,--start-group',a.mesa.resolve(),'-lstdc++','-lexpat','-lzstd','-lz','-lnx','-lm','-Wl,--end-group'],'link')
    undefined=run([a.nm,'--undefined-only','--format=posix',elf],'undefined').splitlines()
    if any(len(p:=line.split())>=2 and p[1]=='U' for line in undefined):raise ValueError('Unresolved strong symbols')
    elf_info=run([a.readelf,'-h','-l','-d',elf],'elf-headers')
    if 'AArch64' not in elf_info or 'DYN' not in elf_info:raise ValueError('Wrong ELF')
    run([a.nacptool,'--create','FNV PC Renderer Probe','FNV-nx','pc-renderer-hw01',nacp],'nacp')
    run([a.elf2nro,elf,nro,'--nacp='+str(nacp),'--icon='+str(a.icon)],'nro')
    raw=nro.read_bytes();size=struct.unpack_from('<I',raw,24)[0]
    if raw[16:20]!=b'NRO0' or raw[size:size+4]!=b'ASET':raise ValueError('Invalid NRO')
    for at in (32,40,48):
        offset,length=struct.unpack_from('<II',raw,at)
        if offset+length>size:raise ValueError('NRO segment out of bounds')
    if any(sha(Path(p))!=h for p,h in frozen.items()):raise ValueError('Inputs changed')
    result=dict(status='PASS',build='pc-renderer-hw01',nro_sha256=sha(nro),nro_bytes=len(raw),elf_sha256=sha(elf),source_sha256=frozen,
        generated_game_code_embedded=False,game_files_embedded=False,engine_execution_enabled=False,switch_hardware_tested=False,static_vulkan=True,translated_com_present=True)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('fixture-validation','sdk','linked-shaders','mesa','devkitpro','headers','cc','nm','readelf','elf2nro','nacptool','icon','output'):p.add_argument('--'+name,type=Path,required=True)
    build(p.parse_args())

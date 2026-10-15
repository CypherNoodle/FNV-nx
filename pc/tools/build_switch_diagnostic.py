"""Package the accepted ARM64 partial link as a private diagnostic NRO.

Includes generated game code; never publish outputs as reusable tool source.
The application only inspects EXE data and executes its own native probe.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def build(a):
    out=a.output.resolve()
    if out.exists():raise ValueError('Use a fresh output directory')
    pc=Path(__file__).resolve().parents[1];proof=json.loads(a.validation.read_text())
    if proof['status']!='PASS' or proof['partial_link']['unexpected_symbols'] or proof['missing_platform_definitions']:
        raise ValueError('Require an accepted partial link and platform definitions')
    partial=a.validation.resolve().parent/'fnv-d3d9-com-relocatable.o'
    if sha(partial)!=proof['partial_link']['object_sha256']:raise ValueError('Changed partial link')
    frozen={Path(p):h for p,h in proof['source_sha256'].items()}
    # Private archival inputs stay pinned too. Header-only comment changes must
    # be rebuilt in the accepted partial-link proof before packaging.
    frozen[partial]=sha(partial);frozen[a.validation.resolve()]=sha(a.validation)
    source=pc/'switch/diagnostic.c';frozen[source]=sha(source)
    frozen[Path(__file__).resolve()]=sha(Path(__file__).resolve())
    for p in (a.cc.resolve(),a.nm.resolve(),a.readelf.resolve(),a.elf2nro.resolve(),a.nacptool.resolve(),a.icon.resolve(),a.devkitpro.resolve()/'libnx/switch.specs',a.devkitpro.resolve()/'libnx/switch.ld'):
        frozen[p]=sha(p)
    def unchanged():return all(sha(p)==h for p,h in frozen.items())
    if not unchanged():raise ValueError('Accepted inputs changed')
    out.mkdir(parents=True);env=os.environ.copy();env['DEVKITPRO']=a.devkitpro.resolve().as_posix()
    def run(command,name):
        r=subprocess.run([str(x) for x in command],env=env,capture_output=True,text=True)
        (out/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+': '+(r.stdout+r.stderr)[-6000:])
        return r.stdout
    obj=out/'diagnostic.o';elf=out/'fnv-nx.elf';nacp=out/'fnv-nx.nacp';nro=out/'fnv-nx.nro'
    flags=proof['compiler_flags']
    run([a.cc,*flags,'-Wall','-Wextra','-Werror','-I'+str(pc/'native'),'-I'+str(a.devkitpro/'libnx/include'),'-c',source,'-o',obj],'compile')
    run([a.cc,'-specs='+str(a.devkitpro/'libnx/switch.specs'),'-march=armv8-a+crc+crypto','-mtune=cortex-a57','-mtp=soft','-fPIE',
        '-Wl,-Map='+str(out/'fnv-nx.map'),'-o',elf,obj,partial,'-L'+str(a.devkitpro/'libnx/lib'),'-lnx','-lm'],'link')
    undefined=run([a.nm,'--undefined-only','--format=posix',elf],'undefined').splitlines()
    strong=[line for line in undefined if len(parts:=line.split())>=2 and parts[1]=='U']
    if strong:raise ValueError('Unresolved final ELF symbols: '+repr(strong))
    headers=run([a.readelf,'-h','-l','-d',elf],'elf-headers')
    if 'AArch64' not in headers or 'DYN' not in headers:raise ValueError('Wrong ELF architecture/type')
    run([a.nacptool,'--create','FNV PC Diagnostic','FNV-nx','pc-diag01',nacp],'nacp')
    run([a.elf2nro,elf,nro,'--nacp='+str(nacp),'--icon='+str(a.icon)],'nro')
    raw=nro.read_bytes()
    if raw[16:20]!=b'NRO0':raise ValueError('Missing NRO header')
    size=struct.unpack_from('<I',raw,24)[0]
    if raw[size:size+4]!=b'ASET':raise ValueError('Missing icon/NACP asset header')
    for at in (32,40,48):
        offset,length=struct.unpack_from('<II',raw,at)
        if offset+length>size:raise ValueError('NRO segment out of bounds')
    if not unchanged():raise ValueError('Inputs changed during packaging')
    result=dict(status='PASS',build='pc-diag01',source_sha256={str(p):h for p,h in frozen.items()},
        partial_link_sha256=sha(partial),elf_sha256=sha(elf),nro_sha256=sha(nro),nro_bytes=len(raw),
        unresolved_strong_symbols=strong,input_compile_units=proof['total_compile_units'],
        engine_execution_enabled=False,switch_hardware_tested=False,game_files_embedded=False,
        generated_game_code_embedded=True)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('validation','devkitpro','cc','nm','readelf','elf2nro','nacptool','icon','output'):p.add_argument('--'+n,type=Path,required=True)
    build(p.parse_args())

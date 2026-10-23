"""Compile the Bink adapter and link a private ARM64/libnx decoder smoke ELF.

No NRO, execution, video playback or game boot is implied by successful linking.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def build(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    root=a.devkitpro.resolve();external=a.ffmpeg_source.resolve();ff=a.ffmpeg_build.resolve()
    proof=json.loads((ff/'validation.json').read_text())
    if proof['status']!='PASS' or proof['target']!='switch':raise ValueError('Require Switch FFmpeg proof')
    for n,h in proof['source_sha256'].items():
        if sha(external/n)!=h:raise ValueError('External sources changed')
    libs=[ff/('lib'+n)/('lib'+n+'.a') for n in ('avformat','avcodec','swscale','swresample','avutil')]
    for p in libs:
        if sha(p)!=proof['library_sha256'][p.parent.name[3:]]:raise ValueError('External library changed')
    cc=root/'devkitA64/bin/aarch64-none-elf-gcc.exe';nm=root/'devkitA64/bin/aarch64-none-elf-nm.exe';readelf=root/'devkitA64/bin/aarch64-none-elf-readelf.exe'
    sources=[pc/'native/bink_decoder.c',pc/'tools/bink_decode_probe.c']
    frozen={str(p):sha(p) for p in (Path(__file__),*sources,pc/'native/bink_decoder.h',pc/'native/host_file.h',pc/'native/host_clock.h',ff/'validation.json',ff/'config.h',cc,nm,readelf,root/'libnx/switch.specs',root/'libnx/switch.ld',root/'libnx/lib/libnx.a',*libs)}
    flags=['-std=gnu11','-O2','-Wall','-Wextra','-Werror','-fPIE','-mtp=soft','-march=armv8-a+crc+crypto','-mtune=cortex-a57','-D__SWITCH__','-I'+str(pc/'native'),'-I'+str(ff),'-I'+str(external),'-I'+str(root/'libnx/include')]
    env=os.environ.copy();env['DEVKITPRO']=root.as_posix();objects=[];commands=[]
    def run(cmd,name):
        r=subprocess.run(list(map(str,cmd)),capture_output=True,text=True,env=env,timeout=120);(out/(name+'.log')).write_text(r.stdout+r.stderr);commands.append(list(map(str,cmd)))
        if r.returncode:raise RuntimeError(r.stderr)
        return r.stdout
    for i,source in enumerate(sources):
        obj=out/(str(i)+'.o');run([cc,*flags,'-c',source,'-o',obj],'compile-'+str(i));objects.append(obj)
    elf=out/'bink-decoder-smoke.elf'
    run([cc,'-specs='+str(root/'libnx/switch.specs'),'-fPIE','-mtp=soft','-march=armv8-a+crc+crypto','-mtune=cortex-a57',*objects,'-L'+str(root/'libnx/lib'),'-Wl,--start-group',*libs,'-lnx','-lm','-Wl,--end-group','-o',elf],'link')
    undefined=run([nm,'--undefined-only','--format=posix',elf],'undefined')
    if any(len(row:=line.split())>1 and row[1]=='U' for line in undefined.splitlines()):raise ValueError('Unresolved strong symbols')
    description=run([readelf,'-h','-l','-d',elf],'elf')
    if 'AArch64' not in description or 'DYN' not in description or 'TEXTREL' in description:raise ValueError('Wrong ELF/relocations')
    if any(sha(Path(p))!=h for p,h in frozen.items()):raise RuntimeError('Inputs changed')
    report=dict(status='PASS',commands=commands,source_sha256=frozen,elf_sha256=sha(elf),elf_bytes=elf.stat().st_size,unresolved_strong=0,text_relocations=False,decoded_on_switch=False,nro_created=False,game_started=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k not in ('commands','source_sha256')}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('ffmpeg-source','ffmpeg-build','output','devkitpro'):p.add_argument('--'+n,type=Path,required=True)
    build(p.parse_args())

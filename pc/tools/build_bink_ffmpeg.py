"""Build a minimal external FFmpeg Bink decoder, without modifying its sources.

No DLL/game assets are copied. Libraries and configuration proofs stay private.
The first profile uses portable C, no threads/ASM/network/programs or GPL parts.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import re
import subprocess


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def msys(p):
    s=Path(p).resolve().as_posix()
    if len(s)>2 and s[1]==':':return '/'+s[0].lower()+s[2:]
    return s


def build(a):
    tool_identity=sha(Path(__file__))
    source=a.source.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=a.resume)
    git=Path(a.git).resolve();bash=Path(a.bash).resolve();sdk=Path(a.devkitpro).resolve()
    prefix=[str(git),'-c','safe.directory='+source.as_posix(),'-C',str(source)]
    commit=subprocess.check_output([*prefix,'rev-parse','HEAD'],text=True).strip()
    tracked=subprocess.check_output([*prefix,'ls-files','-z']).split(b'\0')
    names=[p.decode('utf-8') for p in tracked if p and (source/p.decode('utf-8')).is_file()]
    frozen={n:sha(source/n) for n in names}
    flags=['--disable-everything','--disable-autodetect','--disable-programs','--disable-doc',
           '--disable-network','--disable-avdevice','--disable-avfilter','--disable-postproc',
           '--disable-shared','--enable-static','--disable-gpl','--disable-nonfree','--disable-version3',
           '--disable-pthreads','--disable-w32threads','--disable-os2threads','--disable-asm',
           '--disable-runtime-cpudetect','--disable-debug','--disable-symver','--disable-stripping',
           '--enable-avformat','--enable-avcodec','--enable-avutil','--enable-swscale','--enable-swresample',
           '--enable-demuxer=bink','--enable-decoder=bink,binkaudio_dct,binkaudio_rdft',
           '--enable-cross-compile','--host-cc=clang','--pkg-config=false']
    if a.target=='windows':
        cc=sdk/'msys2/clang64/bin/clang.exe'
        flags+=['--arch=x86_64','--target-os=mingw32','--cc='+msys(cc),'--ld='+msys(cc),
                '--ar=llvm-ar','--ranlib=llvm-ranlib']
    else:
        cc=sdk/'devkitA64/bin/aarch64-none-elf-gcc.exe'
        flags+=['--arch=aarch64','--cpu=cortex-a57','--target-os=none','--cc='+msys(cc),
                '--ar='+msys(sdk/'devkitA64/bin/aarch64-none-elf-ar.exe'),
                '--ranlib='+msys(sdk/'devkitA64/bin/aarch64-none-elf-ranlib.exe'),
                '--extra-cflags=-fPIE -ffunction-sections -fdata-sections -mno-outline-atomics -mtp=soft -D__SWITCH__ -I'+msys(sdk/'libnx/include'),
                '--extra-ldflags=-fPIE -mtp=soft -specs='+msys(sdk/'libnx/switch.specs')+' -L'+msys(sdk/'libnx/lib'),
                '--extra-libs=-lnx -lm']
    env=os.environ.copy();env['DEVKITPRO']=sdk.as_posix();env['DEVKITA64']=(sdk/'devkitA64').as_posix()
    shell_prefix='export PATH='+shlex.quote(msys(sdk/'msys2/clang64/bin')+':'+msys(sdk/'msys2/usr/bin'))+'; '
    command=[str(bash),'--noprofile','--norc',msys(source/'configure'),*flags]
    # Native Windows PATH is translated by MSYS; an explicit shell path also
    # ensures host helper builds use the selected compiler, not another SDK.
    configure=shell_prefix+'exec '+shlex.join(command[1:])
    configure=configure.replace('exec --noprofile --norc ','exec bash --noprofile --norc ',1)
    if a.resume:
        config=(out/'config.h').read_text()
        match=re.search(r'^#define FFMPEG_CONFIGURATION (".*")$',config,re.M)
        if not match or shlex.split(json.loads(match[1]))!=flags:raise ValueError('Resume configuration differs')
        make_config=(out/'ffbuild/config.mak').read_text()
        source_match=re.search(r'^SRC_PATH=(.*)$',make_config,re.M)
        if not source_match or source_match[1]!=msys(source):raise ValueError('Resume source tree differs')
    else:
        print('configure '+a.target,flush=True)
        with (out/'configure.log').open('w') as log:
            r=subprocess.run([str(bash),'--noprofile','--norc','-c',configure],cwd=out,env=env,stdout=log,stderr=log,timeout=300)
        if r.returncode:raise RuntimeError('FFmpeg configure failed; inspect '+str(out/'configure.log'))
    print('make minimal Bink libraries',flush=True)
    with (out/'make.log').open('w') as log:
        r=subprocess.run([str(bash),'--noprofile','--norc','-c',shell_prefix+'exec make -j'+str(a.jobs)],cwd=out,env=env,stdout=log,stderr=log,timeout=600)
    if r.returncode:raise RuntimeError('FFmpeg build failed; inspect '+str(out/'make.log'))
    if any(sha(source/n)!=h for n,h in frozen.items()):raise RuntimeError('External sources changed')
    if sha(Path(__file__))!=tool_identity:raise RuntimeError('Build tool changed during execution')
    libs={n:sha(out/('lib'+n)/('lib'+n+'.a')) for n in ('avformat','avcodec','avutil','swscale','swresample')}
    proof=dict(status='PASS',target=a.target,commit=commit,source_root=str(source),source_sha256=frozen,configure_flags=flags,
               compiler_sha256=sha(cc),tool_sha256=sha(Path(__file__)),library_sha256=libs,
               config_sha256=sha(out/'config.h'),external_sources_unchanged=True,decoded_frames=0,
               switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(proof,indent=2)+'\n')
    print(json.dumps({k:proof[k] for k in ('status','target','commit','decoded_frames')}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('source','output','git','bash','devkitpro'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--target',choices=('windows','switch'),required=True)
    p.add_argument('--jobs',type=int,default=4)
    p.add_argument('--resume',action='store_true',help='Reuse only an identical existing configuration')
    a=p.parse_args()
    if not 1<=a.jobs<=16:p.error('jobs must be 1..16')
    build(a)

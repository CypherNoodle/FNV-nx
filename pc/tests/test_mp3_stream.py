"""Check streaming conversion against integer oracle and MP3 queue/chunk invariance.

MP3 files and decoded PCM stay private. This does not validate the underlying
codec against Windows, guest media APIs, audio hardware or game execution.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from fetch_minimp3 import verify
from build_cpu_runtime import SWITCH_FLAGS


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def expected(rate,count):
    seed=12345;frames=[]
    for _ in range(count):
        frame=[]
        for _ in range(2):
            seed=(seed*1664525+1013904223)&0xffffffff;v=seed>>16;frame.append(v if v<32768 else v-65536)
        frames.append(frame)
    out=bytearray()
    for i in range((count*48000+rate-1)//rate):
        j,phase=divmod(i*rate,48000);a=frames[j];b=frames[min(j+1,count-1)]
        for channel in range(2):
            n=a[channel]*(48000-phase)+b[channel]*phase
            out+=struct.pack('<h',(n+24000)//48000 if n>=0 else -((-n+24000)//48000))
    return bytes(out)


def test(a):
    if a.output.exists():raise ValueError('Use a fresh private directory')
    if not a.file or len(a.file)>4:raise ValueError('Supply 1..4 own MP3 paths')
    pc=Path(__file__).resolve().parents[1];dependency=verify(a.minimp3.resolve())
    sources=[pc/'native'/n for n in ('mp3_stream.c','mp3_stream.h','pcm_convert.c','pcm_convert.h','pcm_queue.c','pcm_queue.h','host_mutex.h','host_file.h')]
    files=[*sources,Path(__file__).resolve(),pc/'tests/mp3_stream_host.c',pc/'tests/pcm_convert_host.c',pc/'tools/fetch_minimp3.py',pc/'tools/build_cpu_runtime.py',
        pc/'native/CMakeLists.txt',Path(a.cc[0]).resolve(),*[a.minimp3.resolve()/n for n in dependency['sha256']]]
    if a.arm_cc:
        if not a.libnx:raise ValueError('ARM build requires --libnx')
        files.append(a.arm_cc.resolve());files.extend(sorted((a.libnx.resolve()/'include').rglob('*.h')))
    frozen={str(p):sha(p) for p in files};assets={str(p.resolve()):dict(sha256=sha(p),size=p.stat().st_size,mtime_ns=p.stat().st_mtime_ns) for p in a.file}
    a.output.mkdir(parents=True);out=a.output.resolve()
    def command(args,log):
        r=subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=120)
        (out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(log+' failed: '+r.stdout+r.stderr)
        return r.stdout
    flags=['-std=c11','-O2','-Wall','-Wextra','-Werror','-fno-strict-aliasing','-ffp-contract=off','-D_POSIX_C_SOURCE=200809L',
           '-I'+str(pc/'native'),'-isystem',str(a.minimp3.resolve())]
    command([*a.cc,*flags,pc/'native/pcm_convert.c',pc/'tests/pcm_convert_host.c','-o',out/'convert.exe'],'convert-compiler.log')
    command([*a.cc,*flags,*[pc/'native'/n for n in ('mp3_stream.c','pcm_convert.c','pcm_queue.c')],pc/'tests/mp3_stream_host.c','-o',out/'mp3.exe'],'mp3-compiler.log')
    cases=0
    for rate in (8000,16000,22050,24000,32000,44100,48000):
        for frames in (0,1,2,31,1201):
            reference=expected(rate,frames)
            for block in (1,173):
                name=f'convert-{rate}-{frames}-{block}';pcm=out/(name+'.pcm')
                command([out/'convert.exe',rate,frames,block,pcm,'test'],name+'.log')
                if pcm.read_bytes()!=reference:raise ValueError('Converter oracle mismatch: '+name)
                cases+=1
    tracks=[]
    for index,path in enumerate(a.file):
        variants=[]
        for mode,block in [('read',1),('read',4096),('pump',43)]:
            name=f'mp3-{index}-{mode}-{block}';pcm=out/(name+'.pcm')
            result=json.loads(command([out/'mp3.exe',path.resolve(),pcm,block,mode],name+'.log').strip().splitlines()[-1])
            variants.append(dict(mode=mode,block=block,pcm_sha256=sha(pcm),**result))
        if len({v['pcm_sha256'] for v in variants})!=1:raise ValueError('MP3 chunk/queue samples differ')
        tracks.append(dict(path=str(path.resolve()),variants=variants))
    arm=[]
    if a.arm_cc:
        if not a.libnx:raise ValueError('ARM build requires --libnx')
        for source in (pc/'native/mp3_stream.c',pc/'native/pcm_convert.c',pc/'native/pcm_queue.c'):
            obj=out/(source.stem+'-arm64.o')
            command([a.arm_cc.resolve(),*SWITCH_FLAGS,'-Wall','-Wextra','-Werror','-D_POSIX_C_SOURCE=200809L',
                '-I'+str(pc/'native'),'-I'+str(a.libnx.resolve()/'include'),'-isystem',a.minimp3.resolve(),'-c',source,'-o',obj],source.stem+'-arm.log')
            arm.append(dict(source=source.name,object_sha256=sha(obj),executed=False))
    for name,h in frozen.items():
        if sha(Path(name))!=h:raise ValueError('Source changed: '+name)
    for name,before in assets.items():
        p=Path(name)
        if dict(sha256=sha(p),size=p.stat().st_size,mtime_ns=p.stat().st_mtime_ns)!=before:raise ValueError('Asset changed: '+name)
    result=dict(status='PASS',converter_oracle_cases=cases,mp3_tracks=tracks,dependency=dependency,source_sha256=frozen,
        read_only_assets=assets,arm64_objects=arm,windows_codec_oracle=False,native_engine_started=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(status='PASS',converter_oracle_cases=cases,mp3_tracks=len(tracks),arm64_objects=len(arm),game_audio_integrated=False)))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('minimp3','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--file',type=Path,action='append',required=True);p.add_argument('--arm-cc',type=Path);p.add_argument('--libnx',type=Path)
    p.add_argument('--cc',nargs='+',required=True);test(p.parse_args())

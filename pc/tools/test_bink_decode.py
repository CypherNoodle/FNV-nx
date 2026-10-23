"""Compile the own decoder/probe against a verified external minimal FFmpeg build.

Uses an explicitly supplied user's Bink file read-only. No assets are exported.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import struct


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def run(a):
    pc=Path(__file__).resolve().parents[1];out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    build=a.ffmpeg_build.resolve();source=a.ffmpeg_source.resolve();proof=json.loads((build/'validation.json').read_text())
    if proof['status']!='PASS' or proof['target']!='windows':raise ValueError('Require Windows build proof')
    for name,h in proof['source_sha256'].items():
        if sha(source/name)!=h:raise ValueError('External source changed: '+name)
    libs=[]
    for n in ('avformat','avcodec','swscale','swresample','avutil'):
        p=build/('lib'+n)/('lib'+n+'.a')
        if sha(p)!=proof['library_sha256'][n]:raise ValueError('Library changed')
        libs.append(p)
    cc=a.cc.resolve();native=pc/'native/bink_decoder.c';probe=pc/'tools/bink_decode_probe.c'
    copy=pc/'native/bink_copy.c'
    frozen={str(p):sha(p) for p in (native,copy,probe,pc/'native/bink_copy.h',pc/'native/bink_decoder.h',pc/'native/host_file.h',pc/'native/host_clock.h',Path(__file__),cc,a.video.resolve(),build/'validation.json',*libs)}
    command=[str(cc),'-std=gnu11','-O2','-Wall','-Wextra','-Werror','-I'+str(pc/'native'),'-I'+str(build),'-I'+str(source),str(native),str(copy),str(probe),'-Wl,--start-group',*map(str,libs),'-Wl,--end-group','-lm','-lole32','-luser32','-lbcrypt','-o',str(out/'decode-test.exe')]
    env=os.environ.copy();env['PATH']=str(cc.parent)+os.pathsep+env['PATH']
    r=subprocess.run(command,capture_output=True,text=True,env=env,timeout=90);(out/'build.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr)
    r=subprocess.run([str(out/'decode-test.exe'),str(a.video.resolve()),str(a.frames)],capture_output=True,text=True,env=env,timeout=120);(out/'decode.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    result=json.loads(r.stdout.strip().splitlines()[-1]);assert result['status']=='PASS' and result['video_frames']==min(a.frames,result['total_frames'])
    if result['audio_tracks']:assert result['pcm_stereo_48000_frames']>0
    # Bounded malformed input and I/O failures; no source video mutation.
    bad=out/'invalid.bik';bad.write_bytes(bytes(128))
    negative=subprocess.run([str(out/'decode-test.exe'),str(bad),'1'],capture_output=True,text=True,env=env,timeout=20);assert negative.returncode==4
    for mode in (1,2,3):
        negative=subprocess.run([str(out/'decode-test.exe'),str(a.video.resolve()),'120',str(mode)],capture_output=True,text=True,env=env,timeout=20)
        (out/('negative-'+str(mode)+'.log')).write_text(negative.stdout+negative.stderr)
        assert negative.returncode in (4,6),negative.stdout+negative.stderr
    # Preserve two original compressed frame packets in a private shortened
    # container to exercise genuine EOF/drain, not just an iteration limit.
    with a.video.open('rb') as stream:
        h=struct.unpack('<11I',stream.read(44))
        if h[0] not in (0x694b4942,0x684b4942,0x674b4942,0x664b4942):raise ValueError('Short fixture supports Bink f/g/h/i')
        stream.seek(44+12*h[10]);indices=struct.unpack('<3I',stream.read(12));stop=indices[2]&~1
        if stop>16*1024*1024 or stop<64:raise ValueError('Short fixture exceeds bounds')
        stream.seek(0);prefix=bytearray(stream.read(stop));assert len(prefix)==stop
    declared=0
    for offset in indices[:2]:
        at=offset&~1;size=struct.unpack_from('<I',prefix,at)[0]
        if h[10]==1 and size>=4:declared+=struct.unpack_from('<I',prefix,at+4)[0]//(2*result['source_audio_channels'])
    struct.pack_into('<I',prefix,4,stop-8);struct.pack_into('<I',prefix,8,2)
    struct.pack_into('<I',prefix,12,max((indices[i+1]&~1)-(indices[i]&~1) for i in range(2)))
    short=out/'short-private.bik';short.write_bytes(prefix)
    eof=subprocess.run([str(out/'decode-test.exe'),str(short),'3'],capture_output=True,text=True,env=env,timeout=30);(out/'eof.log').write_text(eof.stdout+eof.stderr)
    assert not eof.returncode,eof.stdout+eof.stderr
    eof_result=json.loads(eof.stdout.strip().splitlines()[-1]);assert eof_result['video_frames']==2
    if h[10]==1 and result['source_audio_rate']==48000:assert eof_result['pcm_stereo_48000_frames']==declared
    if any(sha(Path(p))!=h for p,h in frozen.items()):raise RuntimeError('Input changed')
    result.update(source_sha256=frozen,packed_frames_checked=result['video_frames'],negative_cases=4,eof_video_frames=2,eof_pcm_frames=eof_result['pcm_stereo_48000_frames'],external_commit=proof['commit'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('ffmpeg-source','ffmpeg-build','video','output','cc'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--frames',type=int,default=120);a=p.parse_args()
    if not 1<=a.frames<=3000:p.error('frames must be 1..3000')
    run(a)

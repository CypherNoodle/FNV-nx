"""Read-only Bink decoding through the real bounded DirectSound movie mixer.

Hashes must match an independently recorded decoder-only run of the same media.
No device playback, guest engine boot or Switch execution is implied.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import subprocess
import test_windows_heap as shared


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def run(a):
    pc=Path(__file__).resolve().parents[1];ff=a.ffmpeg_build.resolve();source=a.ffmpeg_source.resolve()
    proof=json.loads((ff/'validation.json').read_text());assert proof['status']=='PASS' and proof['target']=='windows'
    for n,h in proof['source_sha256'].items():assert sha(source/n)==h,n
    libs=[ff/('lib'+n)/('lib'+n+'.a') for n in ('avformat','avcodec','swscale','swresample','avutil')]
    for p in libs:assert sha(p)==proof['library_sha256'][p.parent.name[3:]]
    reference=None
    extra=[Path(__file__),pc/'native/bink_stream.h',pc/'native/bink_copy.h',pc/'native/bink_decoder.h',pc/'native/dsound8.h',pc/'native/pcm_queue.h',pc/'native/dsound_listener.inc',pc/'tools/bink_decode_probe.c',a.video.resolve(),ff/'validation.json',*libs]
    if a.reference:
        reference=json.loads(a.reference.read_text());assert reference['status']=='PASS'
        for n,h in reference['source_sha256'].items():assert sha(Path(n))==h,n
        assert reference['source_sha256'].get(str(a.video.resolve()))==sha(a.video)
        assert reference['video_frames']==min(a.frames,reference['total_frames']);extra.append(a.reference.resolve())
    links=['-Wl,--start-group',*map(str,libs),'-Wl,--end-group','-lm','-lole32','-luser32','-lbcrypt','-Wl,--stack,8388608']
    a.cc.extend(['-I'+str(ff),'-I'+str(source)])
    if a.x87_build:
        sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve()),str(pc/'tools')]
        from test_x87_environment import verify
        core=json.loads(a.x87_build.read_text());verify(core);assert not core['switch_abi']
        objects=[Path(o['object']) for o in core['objects']]
        extra.extend([a.x87_build.resolve(),*objects,*map(Path,core['source_sha256'])]);links.extend(map(str,objects));a.cc.append('-DFNV_CPU_EXTENDED_X87=1')
    shared.NAMES=['GetLastError'];shared.ARGS=[0];shared.DLLS=['kernel32.dll']
    previous={n:os.environ.get(n) for n in ('FNV_TEST_BINK_VIDEO','FNV_TEST_BINK_FRAMES')}
    try:
        os.environ['FNV_TEST_BINK_VIDEO']=str(a.video.resolve());os.environ['FNV_TEST_BINK_FRAMES']=str(a.frames)
        shared.test(a,host_source='bink_mixer_host.c',native_sources=('dsound8.c','pcm_queue.c','bink_decoder.c','bink_copy.c','bink_stream.c'),extra_inputs=extra,linker_options=links,strict_modes=())
    finally:
        for n,v in previous.items():
            if v is None:os.environ.pop(n,None)
            else:os.environ[n]=v
    p=a.output/'validation.json';result=json.loads(p.read_text())
    if reference is None:
        executable=a.output/'decoder-reference.exe'
        cmd=[*a.cc,'-std=gnu11','-O2','-I'+str(pc/'native'),str(pc/'native/bink_decoder.c'),str(pc/'native/bink_copy.c'),str(pc/'tools/bink_decode_probe.c'),'-Wl,--start-group',*map(str,libs),'-Wl,--end-group','-lm','-lole32','-luser32','-lbcrypt','-o',str(executable)]
        env=shared.compiler_environment(a.cc);r=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=60);(a.output/'reference-build.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
        r=subprocess.run([str(executable),str(a.video.resolve()),str(a.frames)],capture_output=True,text=True,env=env,timeout=120);(a.output/'reference.log').write_text(r.stdout+r.stderr);assert not r.returncode,r.stderr
        reference=json.loads(r.stdout.strip().splitlines()[-1]);assert reference['status']=='PASS'
        result['reference_exe_sha256']=sha(executable)
    assert all(sha(Path(n))==h for n,h in result['source_sha256'].items()),'Inputs changed'
    for field,target in [('video_frames','video_frames'),('pcm_frames','pcm_stereo_48000_frames'),('rgba_hash','rgba_hash'),('pcm_hash','pcm_hash')]:assert result[field]==reference[target],(field,result[field],reference[target])
    if reference['pcm_stereo_48000_frames']>48000:assert result['backpressure_waits']>0 and result['output_full_rollback']
    if a.frames>reference['total_frames']:assert result['decoded_eof']
    result['matches_decoder_reference']=True;p.write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output','ffmpeg-source','ffmpeg-build','video'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--reference',type=Path)
    p.add_argument('--cc',nargs='+',required=True);p.add_argument('--x87-build',type=Path);p.add_argument('--frames',type=int,default=120)
    a=p.parse_args()
    if not 1<=a.frames<=3000:p.error('frames must be1..3000')
    run(a)

"""Check renderer index equivalence/churn and exact silent audio cursors."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();pc=Path(__file__).resolve().parents[1]
    a.output.mkdir(parents=True,exist_ok=False)
    sources=[Path(__file__),pc/'tests/resource_audio_optimization_host.c',
             pc/'native/resource_lookup.h',pc/'native/audio_phase.h',Path(a.cc)]
    sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
    frozen={str(x.resolve()):sha(x) for x in sources}
    exe=a.output.resolve()/'resource-audio.exe'
    command=[a.cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
             '-I'+str(pc/'native'),str(sources[1]),'-o',str(exe)]
    subprocess.run(command,check=True)
    r=subprocess.run([str(exe)],capture_output=True,text=True,check=True,timeout=60)
    report=json.loads(r.stdout);assert report['resource_cases']==464192 and report['silent_audio_cases']==200000
    assert all(sha(Path(n))==v for n,v in frozen.items())
    report.update(status='PASS',source_sha256=frozen,compiler_command=command,
                  console_fps_measured=False)
    (a.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('source_sha256','compiler_command')}))

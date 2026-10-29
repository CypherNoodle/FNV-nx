"""Actual Windows D3D9 HAL texture sampling, not a CPU byte-model oracle."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def run(a):
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);pc=Path(__file__).resolve().parents[1]
    source=pc/'tests/d3d9_texture_channels_host.c';native=a.native.resolve();inputs=[Path(__file__).resolve(),source,native/'d3d9_texture_upload.c',native/'d3d9_texture_upload.h']
    def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
    frozen={str(p):sha(p) for p in inputs};exe=out/'texture-channels.exe'
    built=subprocess.run([*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-I'+str(native),str(source),str(native/'d3d9_texture_upload.c'),'-ld3d9','-luser32','-o',str(exe)],capture_output=True,text=True)
    (out/'compiler.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise RuntimeError(built.stderr)
    tested=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60);(out/'run.log').write_text(tested.stdout+tested.stderr)
    if a.expect_mismatch:
        assert tested.returncode==3 and 'channel mismatch format=28' in tested.stderr
        result=dict(status='PASS',wrong_a8_white_detected=True)
    else:
        if tested.returncode:raise RuntimeError(f'{tested.returncode}: '+tested.stdout+tested.stderr)
        result=json.loads(tested.stdout.strip().splitlines()[-1]);assert result['gpu_pixel_checks']==2048 and result['a8_rgb_zero']
    assert frozen=={str(p):sha(p) for p in inputs};result.update(source_sha256=frozen,exe_sha256=sha(exe),switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--native',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--expect-mismatch',action='store_true');p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

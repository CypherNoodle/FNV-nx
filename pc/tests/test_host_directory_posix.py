"""Run the actual POSIX directory adapter against a FIFO and regular files."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',nargs='+');p.add_argument('--output',type=Path,required=True)
    p.add_argument('--compile-only',action='store_true');p.add_argument('--run-only',action='store_true')
    p.add_argument('--target',help='Optional Clang/Zig cross-compilation target')
    a=p.parse_args()
    if os.name=='nt' and not a.compile_only:raise ValueError('Execute on POSIX/WSL, or cross-compile with --compile-only')
    if a.compile_only and a.run_only:raise ValueError('Choose one mode')
    if not a.run_only and a.output.exists():raise ValueError('Use a fresh output directory')
    pc=Path(__file__).resolve().parents[1]
    if not a.run_only:a.output.mkdir(parents=True)
    paths=[Path(__file__).resolve(),pc/'tests/host_directory_posix.c',*[pc/'native'/n for n in ('host_directory.h','host_file.h','host_clock.h','host_wallclock.h')]]
    def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
    frozen={path.relative_to(pc).as_posix():sha(path) for path in paths};binary=a.output/'directory-test'
    if a.run_only:
        prior=json.loads((a.output/'build.json').read_text())
        if prior['source_sha256']!=frozen or prior['exe_sha256']!=sha(binary):raise ValueError('Cross-build identity changed')
    else:
        if not a.cc:raise ValueError('Require --cc for compilation')
        built=subprocess.run([*a.cc,*(['-target',a.target] if a.target else []),'-std=c11','-Wall','-Wextra','-Werror','-O1','-I'+str(pc/'native'),str(pc/'tests/host_directory_posix.c'),'-o',str(binary)],capture_output=True,text=True)
        (a.output/'compiler.log').write_text(built.stdout+built.stderr)
        if built.returncode:raise RuntimeError(built.stderr)
        (a.output/'build.json').write_text(json.dumps(dict(source_sha256=frozen,exe_sha256=sha(binary)),indent=2)+'\n')
    if a.compile_only:
        print('BUILT POSIX test; runtime execution pending');raise SystemExit(0)
    # WSL's /mnt/c (NTFS) cannot create a FIFO. Use the POSIX temporary fs,
    # while retaining build/proof artifacts in the caller's output directory.
    with tempfile.TemporaryDirectory(prefix='fnv-directory-') as directory:
        run=subprocess.run([str(binary.resolve()),directory],capture_output=True,text=True,timeout=10)
    (a.output/'host.log').write_text(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError(run.stderr)
    assert all(sha(pc/path)==digest for path,digest in frozen.items())
    result=json.loads(run.stdout);result.update(source_sha256=frozen,exe_sha256=sha(binary))
    (a.output/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(run.stdout.strip())

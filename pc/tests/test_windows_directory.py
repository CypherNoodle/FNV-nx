"""Guest directory metadata and star patterns compared with Windows."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path
import test_windows_heap as shared
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    p.add_argument('--game-root',type=Path,help='Read-only BSA/ESM directory comparison')
    shared.NAMES=['GetFileAttributesA','GetFileAttributesW','FindFirstFileA','FindFirstFileW','FindNextFileA','FindNextFileW','FindClose','GetFileType','CloseHandle','CreateFileA','GetLastError','SetLastError']
    shared.ARGS=[1,1,2,2,2,2,1,1,1,7,0,1]
    a=p.parse_args()
    shared.test(a,host_source='windows_directory_host.c',extra_inputs=(Path(__file__).resolve(),),wrapper_stride=64,
                strict_modes=('--strict-output','--strict-disabled','--strict-unicode'))
    if a.game_root:
        out=a.output.resolve();exe=out/'heap-test.exe';validation=json.loads((out/'validation.json').read_text());assert hashlib.sha256(exe.read_bytes()).hexdigest()==validation['exe_sha256']
        run=subprocess.run([str(exe),str(out/'fixture.exe'),'--audit-root',str(a.game_root.resolve())],capture_output=True,text=True,timeout=30)
        (out/'game-directory.log').write_text(run.stdout+run.stderr)
        if run.returncode:raise RuntimeError(run.stdout+run.stderr)
        result=json.loads(run.stdout.strip());result['exe_sha256']=validation['exe_sha256'];(out/'game-directory.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

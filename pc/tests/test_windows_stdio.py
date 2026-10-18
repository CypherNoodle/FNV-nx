"""Compare translated GetStdHandle calls with a detached Windows process."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import sys

SELECTORS=[-10,-11,-12,0,1,-1,-9,-13,0x12345678]
SEEDS=[0,0x3456,0xdeadbeef]
COUNTS=[0,1,3,20,512,0xffffffff,0x80000000]


def oracle(output):
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.GetStdHandle.argtypes=[ctypes.c_uint32]
    kernel.GetStdHandle.restype=ctypes.c_void_p
    samples=[]
    for seed in SEEDS:
        for selector in SELECTORS:
            ctypes.set_last_error(ctypes.c_int32(seed).value)
            value=kernel.GetStdHandle(selector)
            samples.append([selector & 0xffffffff,(value or 0) & 0xffffffff,ctypes.get_last_error() & 0xffffffff])
    kernel.SetHandleCount.argtypes=[ctypes.c_uint32]
    kernel.SetHandleCount.restype=ctypes.c_uint32
    counts=[]
    for seed in SEEDS:
        for count in COUNTS:
            ctypes.set_last_error(ctypes.c_int32(seed).value)
            value=kernel.SetHandleCount(count)
            counts.append([count,value,ctypes.get_last_error() & 0xffffffff])
    output.write_text(json.dumps(dict(samples=samples,handle_counts=counts))+'\n')


if __name__=='__main__':
    if len(sys.argv)==3 and sys.argv[1]=='--oracle-output':
        oracle(Path(sys.argv[2]));raise SystemExit(0)
    import test_windows_heap as shared
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--arm-cc',type=Path);p.add_argument('--cc',nargs='+',required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Use fresh output')
    oracle_path=a.output.with_name(a.output.name+'-oracle.json').resolve()
    if oracle_path.exists():raise ValueError('Use fresh oracle output')
    subprocess.run([sys.executable,str(Path(__file__).resolve()),'--oracle-output',str(oracle_path)],
                   creationflags=subprocess.DETACHED_PROCESS,check=True,timeout=15)
    shared.NAMES=['GetStdHandle','GetLastError','SetLastError','GetStartupInfoA','GetStartupInfoW','SetHandleCount']
    shared.ARGS=[1,0,1,1,1,1]
    shared.test(a,host_source='windows_stdio_host.c',extra_inputs=(Path(__file__).resolve(),oracle_path),strict_modes=())
    validation=a.output/'validation.json';result=json.loads(validation.read_text())
    expected=json.loads(oracle_path.read_text())
    matched=all(result[key]==expected[key] for key in ('samples','handle_counts'))
    result.update(status='PASS' if matched else 'WINDOWS_ORACLE_MISMATCH',
        windows_detached_oracle=matched,oracle_sha256=hashlib.sha256(oracle_path.read_bytes()).hexdigest())
    validation.write_text(json.dumps(result,indent=2)+'\n')
    if not matched:raise AssertionError('Detached Windows oracle differs')
    print('Detached Windows oracle: PASS')

"""Exercise the actual Horizon host-thread header with synthetic syscall stubs."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from test_windows_heap import compiler_environment


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',nargs='+',required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    pc=Path(__file__).resolve().parents[1]
    headers=out/'include/switch/kernel';headers.mkdir(parents=True)
    common=out/'include/switch/mock.h'
    common.write_text("#ifndef MOCK_H\n#define MOCK_H\n#include <stdint.h>\n#include <stddef.h>\ntypedef uint32_t Handle;typedef uint32_t Result;typedef struct {Handle handle;} Thread;\n#define CUR_PROCESS_HANDLE 0xffff8001u\n#define CUR_THREAD_HANDLE 0xffff8000u\n#define InfoType_CoreMask 0\n#define R_FAILED(r) ((r)!=0)\n#define R_SUCCEEDED(r) ((r)==0)\nResult threadCreate(Thread*,void(*)(void*),void*,void*,size_t,int,int);\nResult threadStart(Thread*);Result threadClose(Thread*);Result threadWaitForExit(Thread*);\nResult svcGetInfo(uint64_t*,int,Handle,uint64_t);Result svcGetThreadCoreMask(int32_t*,uint64_t*,Handle);Result svcSetThreadCoreMask(Handle,int32_t,uint32_t);\nResult svcSetThreadPriority(Handle,uint32_t);Result svcGetThreadPriority(int32_t*,Handle);void svcSleepThread(int64_t);\n#endif\n")
    for name in ('thread.h','svc.h'):(headers/name).write_text('#include "../mock.h"\n')
    (out/'include/switch/result.h').write_text('#include "mock.h"\n')
    source=pc/'tests/host_thread_switch_host.c';native=pc/'native/host_thread.h'
    frozen={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in (source,native,Path(__file__).resolve())}
    exe=out/'test.exe';command=[*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-D__SWITCH__','-I'+str(out/'include'),'-I'+str(pc/'native'),str(source),'-o',str(exe)]
    env=compiler_environment(a.cc);r=subprocess.run(command,capture_output=True,text=True,env=env)
    (out/'compiler.log').write_text(r.stdout+r.stderr);r.check_returncode()
    r=subprocess.run([str(exe)],capture_output=True,text=True,env=env,timeout=10)
    (out/'host.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise AssertionError(r.stderr)
    report=json.loads(r.stdout);assert all(hashlib.sha256(Path(x).read_bytes()).hexdigest()==v for x,v in frozen.items())
    report.update(source_sha256=frozen,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'}))


if __name__=='__main__':main()

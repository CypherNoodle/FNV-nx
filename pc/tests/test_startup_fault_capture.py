"""Exercise the actual desktop first-fault observer with concurrent failures.

Uses the unchanged own observer prefix, synthetic faults and real Windows
threads. No game executable, translated function, capture or asset is read.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True);pc=Path(__file__).resolve().parents[1]
    sys.path.insert(0,str(pc/'tools'));from run_aot_analysis import sha
    original=pc/'tests/captured_startup_host.c';text=original.read_text()
    marker='int main(int argc,char **argv){'
    if text.count(marker)!=1:raise ValueError('Ambiguous observer prefix')
    prefix=text.split(marker)[0]
    (out/'startup_profile.h').write_text('#define FNV_STARTUP_CAPTURED_TEXT 0\n')
    source=out/'test.c';source.write_text(prefix+r'''
RECOMP_TLS unsigned fnv_probe_math_kind=UINT32_MAX,fnv_probe_math_operation=UINT32_MAX;
static HANDLE gate;
static DWORD WINAPI origin(void *p){
    unsigned id=(unsigned)(uintptr_t)p;
    assert(WaitForSingleObject(gate,10000)==WAIT_OBJECT_0);
    struct fnv_cpu_fault f={0};f.kind=FNV_CPU_BAD_MEMORY;f.address=0x401000+id;
    f.entry=0x402000+id;f.state.gpr[0]=0x100+id;f.state.gpr[4]=0x403000+id;
    f.trace_index=32+id;f.call_count=1000+id;f.trace[0]=0x404000+id;
    snprintf(f.text,sizeof(f.text),"synthetic origin %u",id);
    fnv_probe_site=0x405000+id;fnv_probe_math_kind=id-1;fnv_probe_math_operation=id;
    capture_fault(NULL,&f);return 0;
}
int main(void){
    fnv_probe_site=0x777777;fnv_probe_math_kind=7;fnv_probe_math_operation=8;
    for(unsigned n=0;n<100;n++){
        AcquireSRWLockExclusive(&fault_lock);first_fault_live=0;memset(&first_fault,0,sizeof(first_fault));ReleaseSRWLockExclusive(&fault_lock);
        struct fnv_cpu_fault peer={0};peer.kind=FNV_CPU_PEER_FAULT;capture_fault(NULL,&peer);assert(!first_fault_live);
        gate=CreateEvent(NULL,TRUE,FALSE,NULL);assert(gate);HANDLE threads[2];
        for(unsigned i=0;i<2;i++){threads[i]=CreateThread(NULL,0,origin,(void *)(uintptr_t)(i+1),0,NULL);assert(threads[i]);}
        assert(SetEvent(gate) && WaitForMultipleObjects(2,threads,TRUE,10000)==WAIT_OBJECT_0);
        for(unsigned i=0;i<2;i++){DWORD code;assert(GetExitCodeThread(threads[i],&code) && !code && CloseHandle(threads[i]));}
        assert(CloseHandle(gate));
        assert(first_fault_live && first_fault.kind==FNV_CPU_BAD_MEMORY);unsigned id=first_fault.address-0x401000;
        assert(id==1 || id==2);
        assert(first_fault.entry==0x402000+id && first_fault.state.gpr[0]==0x100+id && first_fault.state.gpr[4]==0x403000+id);
        assert(first_fault.trace_index==32+id && first_fault.call_count==1000+id && first_fault.trace[0]==0x404000+id);
        assert(first_fault_site==0x405000+id && first_math_kind==id-1 && first_math_operation==id && first_fault_thread);
        struct fnv_cpu_fault copy=first_fault;capture_fault(NULL,&peer);assert(!memcmp(&copy,&first_fault,sizeof copy));
        assert(fnv_probe_site==0x777777 && fnv_probe_math_kind==7 && fnv_probe_math_operation==8);
    }
    puts("FIRST_FAULT_COHERENT_100_ROUNDS_200_THREADS");return 0;
}
''')
    flags=['-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-function',
           '-DFNV_CPU_EXTENDED_X87=1','-DFNV_CPU_X87_QUAD_MATH=1']
    includes=['-I'+str(p) for p in (out,pc/'native',pc/'tests',a.sdk/'templates/runtime',a.sdk/'src/kernel')]
    program=out/'test.exe';r=subprocess.run([*a.cc,*flags,*includes,str(source),'-o',str(program)],capture_output=True,text=True)
    (out/'compiler.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr)
    r=subprocess.run([program],capture_output=True,text=True,timeout=30);(out/'host.log').write_text(r.stdout+r.stderr)
    if r.returncode or 'FIRST_FAULT_COHERENT_100_ROUNDS_200_THREADS' not in r.stdout:raise AssertionError('Observer race test failed')
    if original.read_text()!=text:raise ValueError('Observer changed during test')
    paths=[Path(__file__).resolve(),original,pc/'tests/startup_math_state.h',pc/'native/cpu_runtime.h',pc/'native/fnv_sdk_runtime.h']
    report=dict(status='PASS',rounds=100,native_threads=200,observer_source_unmodified=True,
                tls_isolated=True,coherent_first_fault=True,peer_fault_ignored=True,
                source_sha256={str(p):sha(p) for p in paths},game_inputs=False,hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS 100 concurrent first-fault rounds, 200 native threads')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);test(p.parse_args())

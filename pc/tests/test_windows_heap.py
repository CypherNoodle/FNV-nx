"""Translate x86 process-heap imports and compare payload semantics with Windows."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from pe_fixture import write_pe

NAMES=['GetProcessHeap','HeapAlloc','HeapReAlloc','HeapFree','HeapSize','HeapValidate','GetLastError','SetLastError']
ARGS=[0,3,4,3,3,3,0,1]
DLLS=None
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def compiler_environment(command):
    environment=os.environ.copy()
    if sys.platform=='win32':
        compiler=shutil.which(str(command[0]))
        if compiler:
            environment['PATH']=str(Path(compiler).resolve().parent)+os.pathsep+environment.get('PATH','')
    return environment

def test(a,host_source='windows_heap_host.c',extra_inputs=(),strict_modes=('--strict-flags',),wrapper_stride=32,guest_programs=None,native_sources=(),codegen_setup=None,translator_options=None,linker_options=(),cdecl_imports=()):
    pc=Path(__file__).resolve().parents[1];sdk=a.sdk.resolve();out=a.output.resolve()
    if out.exists():raise ValueError('Use a fresh private directory')
    sys.path[:0]=[str(sdk),str(a.python_deps.resolve()),str(pc/'tools')]
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    from prepare_aot_image import prepare
    from run_aot_analysis import sdk_identity
    if codegen_setup:codegen_setup()
    out.mkdir(parents=True);sdk_hash=sdk_identity(sdk)
    frozen={p:sha(p) for p in [Path(__file__).resolve(),pc/'tests/windows_heap_host.c',pc/'tests'/host_source,pc/'tests/pe_fixture.py',*extra_inputs,
        *[pc/'native'/n for n in ('cpu_runtime.c','cpu_runtime.h','fnv_sdk_runtime.h','pe_image.c','pe_image.h',
        'windows_thread.c','windows_thread.h','pe_tls.h','pe_tls.c','windows_heap.inc','windows_sync.inc','host_condition.h','host_clock.h','host_wallclock.h','host_calendar.h','host_timezone.h','timezone_rule.h','windows_calendar.inc','windows_events.inc','windows_workers.inc','windows_time.inc','windows_parameters.inc','windows_environment.inc','windows_charset.inc','command_line.h','windows_files.inc','host_file.h','host_directory.h','windows_directory.inc','host_thread.h','guest_memory.c','guest_memory.h','host_mutex.h','memory_horizon.c','memory_horizon.h','windows_memory.c','windows_memory.h')],
        *[pc/'native'/n for n in native_sources],
        *[p for d in (sdk/'templates/runtime',sdk/'src/kernel') for p in d.rglob('*.h')]]}
    if getattr(a,'arm_cc',None):
        frozen[a.arm_cc.resolve()]=sha(a.arm_cc)
        frozen[pc/'tools/build_cpu_runtime.py']=sha(pc/'tools/build_cpu_runtime.py')
    exe=out/'fixture.exe';write_pe(exe,DLLS or ['KERNEL32.dll']*len(NAMES));raw=bytearray(exe.read_bytes())
    struct.pack_into('<I',raw,0x98+224+36,0x60000020);cursor=0xC00;programs=[]
    for i,(name,argc) in enumerate(zip(NAMES,ARGS)):
        lookup,_,_,_,iat=struct.unpack_from('<5I',raw,0x300+i*20)
        text=b'\0\0'+name.encode()+b'\0';raw[cursor:cursor+len(text)]=text
        struct.pack_into('<I',raw,lookup-0x1000+0x200,cursor-0x200+0x1000);cursor+=len(text)
        push=bytes([0xFF,0x74,0x24,argc*4])*argc
        ret=b'\xC2'+struct.pack('<H',argc*4) if argc else b'\xC3'
        cleanup=bytes([0x83,0xc4,argc*4]) if i in cdecl_imports and argc else b''
        programs.append(push+b'\xFF\x15'+struct.pack('<I',0x400000+iat)+cleanup+ret)
    functions={}
    for i,code in enumerate(programs):
        assert len(code)<wrapper_stride;va=0x401500+i*wrapper_stride;offset=va-0x401000+0x200;raw[offset:offset+len(code)]=code
        functions[va]=dict(start=f'0x{va:08X}',end=va+len(code),size=len(code),name=f'sub_{va:08X}',section='.text',detection_method='entry_point',called_by=[])
    if guest_programs:
        for va,code in guest_programs(raw).items():
            offset=va-0x401000+0x200;assert 0<=offset<len(raw) and offset+len(code)<=len(raw)
            raw[offset:offset+len(code)]=code
            functions[va]=dict(start=f'0x{va:08X}',end=va+len(code),size=len(code),name=f'sub_{va:08X}',section='.text',detection_method='entry_point',called_by=[])
    exe.write_bytes(raw);prepare(exe,out/'image',sha(exe));image=out/'image/fnv-analysis.xbe'
    config.configure_from_xbe(str(image));translator=FunctionTranslator(image.read_bytes(),functions,**(translator_options or {}))
    source='#include "fnv_sdk_runtime.h"\n'+''.join(f'void sub_{va:08X}(void);\n' for va in functions)
    source+=''.join(translator.translate_function(va,f)+'\n' for va,f in functions.items())
    if 'recomp_unimpl(' in source:raise ValueError('Unsupported synthetic instruction')
    source+='recomp_func_t recomp_lookup(uint32_t va){switch(va){'+''.join(f'case 0x{va:08X}u:return sub_{va:08X};' for va in functions)+'default:return NULL;}}\n'
    generated=out/'generated.c';generated.write_text(source)
    command=[*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-label','-Wno-unused-variable','-Wno-unused-but-set-variable',
        '-fno-strict-aliasing','-ffp-contract=off','-I'+str(pc/'native'),'-I'+str(sdk/'templates/runtime'),'-include',str(pc/'native/fnv_sdk_runtime.h'),str(generated),
        *[str(pc/'native'/n) for n in ('cpu_runtime.c','pe_image.c','pe_tls.c','windows_thread.c','guest_memory.c','memory_horizon.c','windows_memory.c')],
        *[str(pc/'native'/n) for n in native_sources],
        str(pc/'tests'/host_source),*linker_options,'-o',str(out/'heap-test.exe')]
    environment=compiler_environment(a.cc)
    built=subprocess.run(command,capture_output=True,text=True,env=environment);(out/'compiler.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise RuntimeError(f'Compiler exit {built.returncode}: '+built.stdout+built.stderr)
    invocation=[str(out/'heap-test.exe'),str(exe)]
    run=subprocess.run(invocation,capture_output=True,text=True,timeout=45,env=environment);(out/'host.log').write_text(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError(f'Memory bridge exit {run.returncode}: '+run.stdout+run.stderr)
    result=json.loads(run.stdout.strip().splitlines()[-1])
    for mode in strict_modes:
        child=subprocess.run([*invocation,mode],capture_output=True,text=True,timeout=15,env=environment)
        (out/(mode+'.log')).write_text(child.stdout+child.stderr)
        assert child.returncode==0 and 'strict PASS' in child.stdout,(mode,child.returncode,child.stderr)
    if getattr(a,'arm_cc',None):
        from build_cpu_runtime import SWITCH_FLAGS
        obj=out/'generated-arm64.o'
        command=[str(a.arm_cc.resolve()),*SWITCH_FLAGS,'-I'+str(pc/'native'),'-I'+str(sdk/'templates/runtime'),'-I'+str(sdk/'src/kernel'),'-include',str(pc/'native/fnv_sdk_runtime.h'),'-c',str(generated),'-o',str(obj)]
        arm=subprocess.run(command,capture_output=True,text=True);(out/'arm-compiler.log').write_text(arm.stdout+arm.stderr)
        if arm.returncode:raise RuntimeError(arm.stderr)
        result['arm64_fixture_compile']=dict(object_sha256=sha(obj),compiler_sha256=sha(a.arm_cc),flags=SWITCH_FLAGS,executed=False)
    assert all(sha(p)==h for p,h in frozen.items()) and sdk_identity(sdk)==sdk_hash
    result.update(source_sha256={str(p):h for p,h in frozen.items()},sdk_sha256=sdk_hash,synthetic_functions=len(functions),
        strict_flag_stops=len(strict_modes),exe_sha256=sha(out/'heap-test.exe'),switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);test(p.parse_args())

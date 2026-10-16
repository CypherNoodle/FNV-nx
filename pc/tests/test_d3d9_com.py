"""Actual SDK-translated COM callers -> own bridges -> Windows D3D9 NULLREF."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from pe_fixture import write_pe

SLOTS=[(0,3),(1,1),(2,1),(3,2),(4,3),(91,3),(106,3),
    *[(n,4) for n in range(94,100)],*[(n,4) for n in range(109,115)],(92,2)]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def test(a,host_source='d3d9_com_host.c',slots=None,modes=None,strict_fault_cases=4,extra_args=(),extra_inputs=(),extra_sources=(),extra_includes=(),extra_run_args=(),baseline_lifetime_checks=True,factory_import=False):
    pc=Path(__file__).resolve().parents[1];sdk=a.sdk.resolve();out=a.output.resolve()
    if out.exists():raise ValueError('Use a fresh private output directory')
    sys.path[:0]=[str(sdk),str(a.python_deps.resolve()),str(pc/'tools')]
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    from prepare_aot_image import prepare
    from run_aot_analysis import sdk_identity
    from shader_packages import parse_package
    out.mkdir(parents=True);sdk_hash=sdk_identity(sdk)
    native=['cpu_runtime.c','cpu_runtime.h','fnv_sdk_runtime.h','pe_image.c','pe_image.h',
        'guest_memory.c','guest_memory.h','memory_horizon.c','memory_horizon.h','host_mutex.h',
        'd3d9_constants.c','d3d9_constants.h','d3d9_com.c','d3d9_com.h','d3d9_factory.inc']
    slots=SLOTS if slots is None else slots
    modes=('normal','unknown','unsupported','stale','unregistered','parent','capacity','alias') if modes is None else modes
    inputs=[Path(__file__).resolve(),pc/'tests/d3d9_com_host.c',pc/'tests'/host_source,pc/'tests/pe_fixture.py',pc/'tools/shader_packages.py',
        *[pc/'native'/n for n in native],*[Path(p) for p in (*extra_args,*extra_inputs,*extra_sources)],*sorted(a.shaders.resolve().glob('*.sdp')),
        *[p for d in (sdk/'templates/runtime',sdk/'src/kernel') for p in d.rglob('*.h')]]
    frozen={p:sha(p) for p in inputs}
    unique={}
    for p in sorted(a.shaders.glob('*.sdp')):
        for r in parse_package(p.read_bytes()):unique.setdefault(r['sha256'],r)
    if not unique:raise ValueError('Empty shader catalog')
    catalog=out/'catalog.bin'
    with catalog.open('wb') as f:
        f.write(b'FNV9SHD1'+struct.pack('<I',len(unique)))
        for key in sorted(unique):
            r=unique[key];f.write(struct.pack('<2I',int(r['stage']=='pixel'),r['bytes'])+r['code'])
    fixture=out/'fixture.exe';write_pe(fixture);raw=bytearray(fixture.read_bytes())
    if factory_import:
        struct.pack_into('<II',raw,0x98+104,0x1100,40)
        struct.pack_into('<IIIII',raw,0x300,0x1140,0,0,0x1130,0x1180)
        raw[0x330:0x33a]=b'd3d9.dll\0\0'
        struct.pack_into('<II',raw,0x340,0x1160,0)
        raw[0x360:0x372]=b'\0\0Direct3DCreate9\0'
        struct.pack_into('<II',raw,0x380,0x1160,0)
    # Two complete text pages allow a full pipeline fixture without overlapping
    # data or depending on bytes beyond the declared PE section.
    if len(slots)>108:raise ValueError('Callers exceed the fixture text pages')
    raw.extend(bytes(0x2200-len(raw)))
    struct.pack_into('<I',raw,0x98+56,0x3000)
    struct.pack_into('<I',raw,0x98+224+8,0x2000)
    struct.pack_into('<I',raw,0x98+224+16,0x2000)
    struct.pack_into('<I',raw,0x98+224+36,0x60000020);functions={}
    for index,(slot,argc) in enumerate(slots):
        # COM stdcall: explicit this on stack, not ECX. Re-push caller args.
        if slot is None:
            assert factory_import and argc==1
            code=bytes.fromhex('ff742404ff15')+struct.pack('<I',0x401180)+bytes.fromhex('c20400')
        else:
            code=bytes.fromhex('8b4424048b10')+bytes([0xFF,0x74,0x24,argc*4])*argc
            code+=bytes.fromhex('ff92')+struct.pack('<I',slot*4)+b'\xC2'+struct.pack('<H',argc*4)
        assert len(code)<64;at=0x401500+index*64;offset=at-0x401000+0x200
        raw[offset:offset+len(code)]=code
        functions[at]=dict(start=f'0x{at:08X}',end=at+len(code),size=len(code),name=f'sub_{at:08X}',section='.text',detection_method='entry_point',called_by=[])
    fixture.write_bytes(raw);prepare(fixture,out/'image',sha(fixture));image=out/'image/fnv-analysis.xbe'
    config.configure_from_xbe(str(image));translator=FunctionTranslator(image.read_bytes(),functions)
    source='#include "fnv_sdk_runtime.h"\n'+''.join(f'void sub_{at:08X}(void);\n' for at in functions)
    source+=''.join(translator.translate_function(at,r)+'\n' for at,r in functions.items())
    if 'recomp_unimpl(' in source:raise ValueError('Unsupported synthetic instruction')
    source+='recomp_func_t recomp_lookup(uint32_t at){switch(at){'+''.join(f'case 0x{at:08X}u:return sub_{at:08X};' for at in functions)+'default:return NULL;}}\n'
    generated=out/'generated.c';generated.write_text(source);exe=out/'com-test.exe'
    sources=['cpu_runtime.c','pe_image.c','guest_memory.c','memory_horizon.c','d3d9_constants.c','d3d9_com.c']
    command=[*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-label','-Wno-unused-variable','-Wno-unused-but-set-variable',
        '-fno-strict-aliasing','-ffp-contract=off','-I'+str(pc/'native'),'-I'+str(sdk/'templates/runtime'),
        '-include',str(pc/'native/fnv_sdk_runtime.h'),str(generated),*[str(pc/'native'/n) for n in sources],
        *['-I'+str(Path(p).resolve()) for p in extra_includes],*[str(Path(p).resolve()) for p in extra_sources],
        str(pc/'tests'/host_source),'-ld3d9','-luser32','-o',str(exe)]
    built=subprocess.run(command,capture_output=True,text=True);(out/'compiler.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise RuntimeError(built.stderr)
    invocation=[str(exe),str(fixture),str(catalog)];result=None
    for mode in modes:
        run=subprocess.run([*invocation,mode,*[str(p) for p in (*extra_args,*extra_run_args)]],capture_output=True,text=True,timeout=60)
        (out/(mode+'.log')).write_text(run.stdout+run.stderr)
        if run.returncode:raise RuntimeError(f'{mode}: exit {run.returncode}: '+run.stdout+run.stderr)
        parsed=json.loads(run.stdout.strip().splitlines()[-1])
        assert parsed['status']=='PASS'
        if mode=='normal':result=parsed
    if any(sha(p)!=h for p,h in frozen.items()) or sdk_identity(sdk)!=sdk_hash:raise ValueError('Inputs changed')
    result.update(source_sha256={str(p):h for p,h in frozen.items()},sdk_sha256=sdk_hash,exe_sha256=sha(exe),
        synthetic_functions=len(functions),strict_fault_cases=strict_fault_cases,parent_lifetime_checked=baseline_lifetime_checks,capacity_and_alias_checked=baseline_lifetime_checks,
        native_engine_started=False,switch_hardware_tested=False,gpu_rendered=result.get('gpu_rendered',False))
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output','shaders'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);test(p.parse_args())

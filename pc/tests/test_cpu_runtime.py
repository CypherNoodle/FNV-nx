"""Real host threads + actual synthetic SDK C, import ABI and fail-stop tests."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from pe_fixture import write_pe


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def test(sdk,deps,cc,output,game=None):
    pc=Path(__file__).resolve().parents[1]
    sys.path[:0]=[str(sdk.resolve()),str(deps.resolve()),str(pc/'tools')]
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    from prepare_aot_image import prepare
    from run_aot_analysis import sdk_identity
    if output.exists():raise ValueError('Use a fresh private validation directory')
    output.mkdir(parents=True);sdk_hash=sdk_identity(sdk)
    own=[pc/'native/cpu_runtime.c',pc/'native/cpu_runtime.h',pc/'native/cpu_native_observer.h',pc/'native/fnv_sdk_runtime.h',pc/'native/pe_image.c',pc/'native/pe_image.h',pc/'tests/cpu_runtime_host.c',Path(__file__).resolve(),pc/'tests/pe_fixture.py']
    identities={p.name:sha(p) for p in own}
    sdk_headers={p:sha(p) for directory in (sdk/'templates/runtime',sdk/'src/kernel') for p in directory.rglob('*.h')}
    game_exe=game/'FalloutNV.exe' if game else None
    game_sha=sha(game_exe) if game_exe else None
    exe=output/'fixture.exe';write_pe(exe,('FIXTURE.dll','Fixture.dll'))
    raw=bytearray(exe.read_bytes());struct.pack_into('<I',raw,0x98+224+36,0x60000020)
    directory=0x300;lookup=struct.unpack_from('<I',raw,directory)[0];iat=struct.unpack_from('<I',raw,directory+16)[0]
    iat2=struct.unpack_from('<I',raw,directory+20+16)[0]
    raw[0xF00:0xF06]=b'\0\0Sum\0';struct.pack_into('<I',raw,lookup-0x1000+0x200,0x1D00)
    marker=struct.pack('<I',0x401c00)
    programs=[bytes.fromhex('5356ff15')+struct.pack('<I',0x400000+iat)+b'\xc3',
        bytes.fromhex('b878563412f4c705')+marker+struct.pack('<I',1)+b'\xc3',
        bytes.fromhex('ffd0c705')+marker+struct.pack('<I',1)+b'\xc3',
        bytes.fromhex('0fa2c3'),bytes.fromhex('0f31c3'),
        bytes.fromhex('ff15')+struct.pack('<I',0x400000+iat2)+b'\xc3',
        bytes.fromhex('64a118000000c3'),bytes.fromhex('ffe0'),
        bytes.fromhex('ff15')+struct.pack('<I',0x400000+iat2)+bytes.fromhex('ebf8')]
    functions={}
    for i,code in enumerate(programs):
        va=0x401500+i*0x80;at=va-0x401000+0x200;raw[at:at+len(code)]=code
        functions[va]=dict(start=f'0x{va:08X}',end=va+len(code),size=len(code),name=f'sub_{va:08X}',section='.text',detection_method='entry_point',called_by=[])
    exe.write_bytes(raw);prepare(exe,output/'image',sha(exe));image=output/'image/fnv-analysis.xbe'
    config.configure_from_xbe(str(image));translator=FunctionTranslator(image.read_bytes(),functions)
    source='#include "fnv_sdk_runtime.h"\n'+''.join(f'void sub_{a:08X}(void);\n' for a in functions)
    for at,entry in functions.items():source+=translator.translate_function(at,entry)+'\n'
    source+='recomp_func_t recomp_lookup(uint32_t va) { switch(va) {\n'+''.join(f'case 0x{a:08X}u:return sub_{a:08X};\n' for a in functions)+'default:return NULL;}}\n'
    generated=output/'generated.c';generated.write_text(source)
    command=[*cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-label','-Wno-unused-variable','-Wno-unused-but-set-variable',
        '-fno-strict-aliasing','-ffp-contract=off','-I'+str((pc/'native').resolve()),'-I'+str((sdk/'templates/runtime').resolve()),
        '-include',str((pc/'native/fnv_sdk_runtime.h').resolve()),str(generated.resolve()),str((pc/'native/cpu_runtime.c').resolve()),
        str((pc/'native/pe_image.c').resolve()),str((pc/'tests/cpu_runtime_host.c').resolve()),'-o',str((output/'runtime-test.exe').resolve())]
    if sys.platform!='win32':command.append('-pthread')
    built=subprocess.run(command,capture_output=True,text=True)
    (output/'compiler.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise RuntimeError(built.stderr)
    invocation=[str((output/'runtime-test.exe').resolve()),str(exe.resolve())]
    if game:invocation.append(str((game/'FalloutNV.exe').resolve()))
    run=subprocess.run(invocation,capture_output=True,text=True,timeout=45)
    (output/'host.log').write_text(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError(f'Host test exit {run.returncode}: '+run.stdout+run.stderr)
    result=json.loads(run.stdout.strip().splitlines()[-1])
    fatal=subprocess.run([str((output/'runtime-test.exe').resolve()),'--fatal'],capture_output=True,text=True,timeout=15)
    (output/'fatal.log').write_text(fatal.stdout+fatal.stderr)
    assert fatal.returncode==77 and 'outside execution boundary' in fatal.stderr
    assert sdk_identity(sdk)==sdk_hash and identities=={p.name:sha(p) for p in own}
    assert all(sha(p)==digest for p,digest in sdk_headers.items())
    assert not game_exe or sha(game_exe)==game_sha
    result.update(synthetic_functions=len(programs),unhandled_fault_terminates=True,sdk_sha256=sdk_hash,source_sha256=identities,
        sdk_runtime_header_sha256={p.relative_to(sdk).as_posix():digest for p,digest in sdk_headers.items()},
        input_game_sha256=game_sha,
        exe_sha256=sha(output/'runtime-test.exe'),native_engine_started=False,limits=['Host synthetic routines, not game execution','No implemented Windows imports','No host fault/Windows SEH emulation'])
    (output/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);p.add_argument('--game',type=Path)
    a=p.parse_args();test(a.sdk,a.python_deps,a.cc,a.output,a.game)

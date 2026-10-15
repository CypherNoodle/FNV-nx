"""Translated x86 IAT calls into native Windows TLS/TEB bridges, no game run."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from pe_fixture import write_pe

NAMES=['TlsAlloc','TlsFree','TlsGetValue','TlsSetValue','GetLastError',
       'SetLastError','GetCurrentThreadId','GetCurrentProcessId']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def test(a):
    pc=Path(__file__).resolve().parents[1];sdk=a.sdk.resolve();out=a.output.resolve()
    if out.exists():raise ValueError('Use a fresh private output directory')
    sys.path[:0]=[str(sdk),str(a.python_deps.resolve()),str(pc/'tools')]
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    from prepare_aot_image import prepare
    from run_aot_analysis import sdk_identity
    out.mkdir(parents=True);sdk_sha=sdk_identity(sdk)
    frozen={p:sha(p) for p in [Path(__file__).resolve(),pc/'tests/windows_thread_host.c',pc/'tests/pe_fixture.py',
        *[pc/'native'/n for n in ('windows_thread.c','windows_thread.h','cpu_runtime.c','cpu_runtime.h','fnv_sdk_runtime.h','pe_image.c','pe_image.h')],
        *[p for d in (sdk/'templates/runtime',sdk/'src/kernel') for p in d.rglob('*.h')]]}
    exe=out/'fixture.exe';write_pe(exe,['KERNEL32.dll']*8)
    raw=bytearray(exe.read_bytes());struct.pack_into('<I',raw,0x98+224+36,0x60000020)
    programs=[];names_cursor=0xC00
    for i,name in enumerate(NAMES):
        lookup,_,_,_,iat=struct.unpack_from('<5I',raw,0x300+i*20)
        data=b'\0\0'+name.encode()+b'\0';raw[names_cursor:names_cursor+len(data)]=data
        symbol_rva=names_cursor-0x200+0x1000
        struct.pack_into('<I',raw,lookup-0x1000+0x200,symbol_rva)
        # Lookup and IAT initially alias, as in the original parser fixture.
        names_cursor+=len(data)
        args=b'\x56\x53' if name=='TlsSetValue' else b'\x53' if name in ('TlsFree','TlsGetValue','SetLastError') else b''
        programs.append(args+b'\xff\x15'+struct.pack('<I',0x400000+iat)+b'\xc3')
    for offset in (0,4,8,0x18,0x20,0x24,0x2C,0x30,0x34):
        programs.append(b'\x64\xa1'+struct.pack('<I',offset)+b'\xc3')
    programs += [bytes.fromhex('648b049d100e0000c3'),bytes.fromhex('64a1940f00008b0498c3'),bytes.fromhex('64891d34000000c3')]
    functions={}
    for i,code in enumerate(programs):
        va=0x401500+i*0x20;at=va-0x401000+0x200;raw[at:at+len(code)]=code
        functions[va]=dict(start=f'0x{va:08X}',end=va+len(code),size=len(code),name=f'sub_{va:08X}',section='.text',detection_method='entry_point',called_by=[])
    assert 0x1500+len(programs)*0x20<0x1A00
    exe.write_bytes(raw);prepare(exe,out/'image',sha(exe))
    image=out/'image/fnv-analysis.xbe';config.configure_from_xbe(str(image))
    translator=FunctionTranslator(image.read_bytes(),functions)
    source='#include "fnv_sdk_runtime.h"\n'+''.join(f'void sub_{va:08X}(void);\n' for va in functions)
    source+=''.join(translator.translate_function(va,f)+'\n' for va,f in functions.items())
    if 'recomp_unimpl(' in source:raise ValueError('Synthetic test contains unsupported instructions')
    source+='recomp_func_t recomp_lookup(uint32_t va) {switch(va){'+''.join(f'case 0x{va:08X}u:return sub_{va:08X};' for va in functions)+'default:return NULL;}}\n'
    generated=out/'generated.c';generated.write_text(source)
    command=[*a.cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-label','-Wno-unused-variable','-Wno-unused-but-set-variable',
        '-fno-strict-aliasing','-ffp-contract=off','-I'+str(pc/'native'),'-I'+str(sdk/'templates/runtime'),
        '-include',str(pc/'native/fnv_sdk_runtime.h'),str(generated),
        *[str(pc/'native'/n) for n in ('cpu_runtime.c','windows_thread.c','pe_image.c')],
        str(pc/'tests/windows_thread_host.c'),'-o',str(out/'windows-test.exe')]
    if sys.platform!='win32':command.append('-pthread')
    built=subprocess.run(command,capture_output=True,text=True)
    (out/'compiler.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise RuntimeError(built.stderr)
    invocation=[str(out/'windows-test.exe'),str(exe)]
    game_sha=None
    game_import_matches=[]
    if a.game:
        game_exe=a.game/'FalloutNV.exe';game_sha=sha(game_exe);invocation.append(str(game_exe.resolve()))
        from audit_install import pe_info
        game_import_matches=[symbol['name'] for dll in pe_info(game_exe)['imports']
            if dll['dll'].lower()=='kernel32.dll' for symbol in dll['symbols'] if symbol.get('name') in NAMES]
    run=subprocess.run(invocation,capture_output=True,text=True,timeout=60)
    (out/'host.log').write_text(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError(f'Test exit {run.returncode}: '+run.stdout+run.stderr)
    result=json.loads(run.stdout.strip().splitlines()[-1])
    assert sdk_identity(sdk)==sdk_sha and all(sha(p)==h for p,h in frozen.items())
    assert not a.game or sha(game_exe)==game_sha
    result.update(synthetic_functions=len(functions),sdk_sha256=sdk_sha,source_sha256={str(p):h for p,h in frozen.items()},
        input_game_sha256=game_sha,game_import_name_matches=game_import_matches,
        exe_sha256=sha(out/'windows-test.exe'),native_engine_started=False,
        limits=['Host synthetic x86 routines, no hardware run','No PE static TLS/callback initialization','No CreateThread, guest heap or SEH implementation'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--cc',nargs='+',required=True);parser.add_argument('--game',type=Path)
    test(parser.parse_args())

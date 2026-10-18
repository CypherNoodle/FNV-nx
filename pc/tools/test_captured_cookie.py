"""Privately translate/execute one audited cookie initializer, never an EXE entry.

Requires explicit bounds and data addresses. No DLL/EXE patching or startup
claim. Generated code, loaded data and compiled artifacts must remain private.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from audit_aot_calls import inspect
from audit_startup import audit
from run_aot_analysis import sdk_identity,sha

NAMES=('GetSystemTimeAsFileTime','GetCurrentProcessId','GetCurrentThreadId','GetTickCount','QueryPerformanceCounter')


def run(a):
    if a.output.exists():raise ValueError('Use a fresh private output directory')
    if not 1<=a.end-a.entry<=1024:raise ValueError('Require explicit helper bounds <=1024 bytes')
    sys.path[:0]=[str(a.python_deps.resolve()),str(a.sdk.resolve())]
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32,CS_GRP_JUMP
    from capstone.x86_const import X86_OP_IMM,X86_OP_MEM
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    from aot_frame_restore import install
    pc=Path(__file__).resolve().parents[1];analysis=a.analysis.resolve()
    identity=audit(a.exe,analysis);report=inspect(a.exe,analysis,[a.entry],1000)
    root=report['roots'][0]
    if root['limit_reached'] or len(root['calls'])!=5 or {c.get('import') for c in root['calls']}!={'KERNEL32.dll!'+n for n in NAMES}:
        raise ValueError('Require only the five audited cookie initialization imports')
    if any(c['target'] is not None for c in root['calls']) or any(s['reason']!='ret' for s in root['stops']):
        raise ValueError('Not a closed, bounded leaf routine')
    p=json.loads((analysis/'input-profile.json').read_text());image=(analysis/'fnv-analysis.xbe').read_bytes()
    count,table=struct.unpack_from('<II',image,0x11c)
    sections=[struct.unpack_from('<5I',image,table-p['image_base']+i*56) for i in range(count)]
    row=next(r for r in sections if r[1]<=a.entry and a.end<=r[1]+r[4])
    code=image[row[3]+a.entry-row[1]:row[3]+a.end-row[1]]
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True;instructions=list(decoder.disasm(code,a.entry))
    if not instructions or sum(i.size for i in instructions)!=len(code) or instructions[-1].mnemonic!='ret':raise ValueError('Incomplete leaf decode')
    for ins in instructions:
        if ins.group(CS_GRP_JUMP) and (ins.operands[0].type!=X86_OP_IMM or not a.entry<=ins.operands[0].imm<a.end):
            raise ValueError('Branch leaves selected routine')
    slots={}
    for ins in instructions:
        if ins.mnemonic=='call':
            op=ins.operands[0]
            if op.type!=X86_OP_MEM or op.mem.base or op.mem.index or op.mem.segment:raise ValueError('Non-IAT call')
            found=next(c for c in root['calls'] if c['site']==ins.address)
            slots[found['import'].split('!',1)[1]]=op.mem.disp & 0xffffffff
    for address in (a.cookie,a.complement):
        if not any(s['name']=='.data' and p['image_base']+s['rva']<=address and address+4<=p['image_base']+s['rva']+s['virtual_bytes'] for s in p['sections']):
            raise ValueError('Cookie must be in data')
    if a.cookie==a.complement:raise ValueError('Cookie/complement overlap')
    # No existing image import table is rebound. Only these five explicit IAT
    # slots get registered native tokens in the private in-memory test arena.
    sdk_hash=sdk_identity(a.sdk.resolve());host=pc/'tests/captured_cookie_host.c'
    frozen={str(path.resolve()):sha(path) for path in [Path(__file__),host,pc/'tools/aot_frame_restore.py',
        pc/'tools/audit_aot_calls.py',pc/'tools/audit_startup.py',pc/'tools/audit_install.py',pc/'tools/run_aot_analysis.py',
        *[pc/'native'/n for n in ('cpu_runtime.c','pe_image.c','pe_tls.c','windows_thread.c','guest_memory.c','memory_horizon.c','windows_memory.c')],
        *list((pc/'native').glob('*.h')),*list((pc/'native').glob('*.inc')),
        Path(a.cc[0]).resolve(),
        *[p for d in (a.sdk.resolve()/'templates/runtime',a.sdk.resolve()/'src/kernel') for p in d.rglob('*.h')],
        *([a.arm_cc.resolve(),pc/'tools/build_cpu_runtime.py'] if a.arm_cc else [])]}
    a.output.mkdir(parents=True);out=a.output.resolve()
    config.configure_from_xbe(str(analysis/'fnv-analysis.xbe'))
    if not a.without_overlay:install()
    functions={a.entry:dict(start=hex(a.entry),end=a.end,size=a.end-a.entry,name=f'sub_{a.entry:08X}',section='.text',called_by=[])}
    translator=FunctionTranslator(image,functions,seh_prolog=0,seh_epilog=0)
    source='#include "fnv_sdk_runtime.h"\n'+translator.translate_function(a.entry,functions[a.entry])
    if any(s in source for s in ('recomp_unimpl(','translation failed',': unhandled */')):raise ValueError('Unsupported generated leaf')
    source+=f'\nrecomp_func_t recomp_lookup(uint32_t va){{return va==0x{a.entry:X}u?sub_{a.entry:08X}:NULL;}}\n'
    generated=out/'generated.c';generated.write_text(source)
    header=out/'cookie_profile.h'
    header.write_text('\n'.join(f'#define FNV_COOKIE_{n} 0x{v:X}u' for n,v in [('ENTRY',a.entry),('VALUE',a.cookie),('COMPLEMENT',a.complement)])+
        '\nstatic const uint32_t cookie_slots[5]={'+','.join(hex(slots[n])+'u' for n in NAMES)+'};\n')
    def command(args,log):
        r=subprocess.run([str(s) for s in args],capture_output=True,text=True,timeout=120)
        (out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(str(log)+' exit '+str(r.returncode)+': '+r.stdout+r.stderr)
        return r.stdout
    flags=['-std=c11','-O1','-Wall','-Wextra','-Werror','-Wno-unused-label','-Wno-unused-variable','-Wno-unused-but-set-variable','-fno-strict-aliasing','-ffp-contract=off']
    includes=['-I'+str(d) for d in (out,pc/'native',a.sdk.resolve()/'templates/runtime',a.sdk.resolve()/'src/kernel')]
    native=[pc/'native'/n for n in ('cpu_runtime.c','pe_image.c','pe_tls.c','windows_thread.c','guest_memory.c','memory_horizon.c','windows_memory.c')]
    command([*a.cc,*flags,*includes,'-include',pc/'native/fnv_sdk_runtime.h',generated,*native,host,'-o',out/'cookie-test.exe'],'compiler.log')
    result=json.loads(command([out/'cookie-test.exe',a.exe.resolve()],'host.log').strip().splitlines()[-1])
    if a.arm_cc:
        from build_cpu_runtime import SWITCH_FLAGS
        command([a.arm_cc.resolve(),*SWITCH_FLAGS,*includes,'-include',pc/'native/fnv_sdk_runtime.h','-c',generated,'-o',out/'generated-arm64.o'],'arm-compiler.log')
        result['arm64_compile']=dict(executed=False,sha256=sha(out/'generated-arm64.o'))
    if audit(a.exe,analysis)!=identity or sdk_identity(a.sdk.resolve())!=sdk_hash:raise ValueError('Input identities changed')
    for name,h in frozen.items():
        if sha(Path(name))!=h:raise ValueError('Source changed: '+name)
    result.update(source_sha256=frozen,identity=identity,sdk_sha256=sdk_hash,leaf_sha256=hashlib.sha256(code).hexdigest(),
        generated_sha256=sha(generated),normal_import_table_bound=False,full_crt_started=False,native_engine_started=False,
        switch_hardware_tested=False,frame_restore=not a.without_overlay)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('source_sha256','identity')},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('exe','analysis','sdk','python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    for n in ('entry','end','cookie','complement'):p.add_argument('--'+n,type=lambda s:int(s,0),required=True)
    p.add_argument('--arm-cc',type=Path);p.add_argument('--cc',nargs='+',required=True)
    p.add_argument('--without-overlay',action='store_true',help='Expected negative EBP restoration control')
    run(p.parse_args())

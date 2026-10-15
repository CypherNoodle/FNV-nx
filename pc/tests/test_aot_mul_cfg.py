"""Full SDK function/CFG generation and x86 vs ARM64 multiply-flag tests."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys


def program(branch, tail='0f90c30f91c70f92c2'):
    # MUL EDX; Jcc yes; MOV ECX,1; JMP join; yes:MOV ECX,2;
    # join:SETO BL; SETNO BH; SETB DL; RET. MOVs/JMP preserve flags.
    return bytes.fromhex('f7e2'+branch+'07b901000000eb05b902000000'+tail+'c3')


def test(sdk,deps,compiler,output):
    pc=Path(__file__).resolve().parents[1]
    sys.path[:0]=[str(sdk.resolve()),str(deps.resolve()),str(pc/'tools'),str(pc/'tests')]
    from pe_fixture import write_pe
    from prepare_aot_image import prepare
    from aot_mul_flags import install
    from run_aot_analysis import sdk_identity
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    from unicorn import Uc, UC_ARCH_ARM64, UC_ARCH_X86, UC_MODE_ARM, UC_MODE_32
    import unicorn.arm64_const as ar
    import unicorn.x86_const as xr
    if output.exists():raise ValueError('Use a fresh private output directory')
    output.mkdir(parents=True)
    before_sdk=sdk_identity(sdk); install()
    own=[pc/'tools/aot_mul_flags.py',pc/'native/fnv_mul_flags.h']
    identity={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in own}
    cases=[program(x) for x in ('70','71','72','73')]
    # A flag-preserving jump into a consumer block, with source overwrites.
    cases.append(bytes.fromhex('f7e2eb00b811111111ba222222220f90c10f93c3c3'))
    # A small CFG loop: first test sets ZF; every visited MUL defines CF/OF;
    # DEC sets OF afterwards and leaves CF from the last MUL. SETO must read
    # DEC's overflow, never the old multiply snapshot.
    cases.append(bytes.fromhex('f7e24975fb0f90c30f92c7c3'))  # MUL EDX; DEC ECX; JNE start
    # Mixed producer join intentionally stays unresolved (no fabricated OF).
    unknown=bytes.fromhex('85f67404f7e2eb030fafc20f90c1c3')
    all_cases=cases+[unknown]
    exe=output/'fixture.exe'; write_pe(exe)
    raw=bytearray(exe.read_bytes()); struct.pack_into('<I',raw,0x98+224+36,0x60000020)
    functions={}
    for number,code in enumerate(all_cases):
        at=0x200+number*0x80; address=0x401000+number*0x80
        raw[at:at+len(code)]=code
        functions[address]=dict(start=f'0x{address:08X}',end=address+len(code),size=len(code),
            name=f'sub_{address:08X}',section='.text',detection_method='entry_point',called_by=[])
    exe.write_bytes(raw)
    profile=prepare(exe,output/'image',hashlib.sha256(raw).hexdigest())
    image=output/'image/fnv-analysis.xbe'; config.configure_from_xbe(str(image))
    translator=FunctionTranslator(image.read_bytes(),functions)
    generated=[]
    for number,code in enumerate(cases):
        address=0x401000+number*0x80
        body=translator.translate_function(address,functions[address])
        assert body and 'uint32_t _fnv_mul_overflow = 0;' in body
        assert 'RECOMP_UNIMPL' not in body,body
        generated.append(body)
    address=0x401000+len(cases)*0x80
    unsupported=translator.translate_function(address,functions[address])
    assert 'RECOMP_UNIMPL("seto' in unsupported
    source='''#define RECOMP_GENERATED_CODE
#include "fnv_mul_flags.h"
typedef struct {uint32_t gpr[8]; unsigned char memory[128]; uint32_t seh;} State;
_Static_assert(sizeof(State)==164,"State layout");
ptrdiff_t g_xbox_mem_offset;
static State *active;
'''
    for i,r in enumerate(('eax','ecx','edx','ebx','esp','ebp','esi','edi')):
        source+=f'#define g_{r} (active->gpr[{i}])\n'
    source+='#define g_seh_ebp (active->seh)\n'
    source+='\n'.join(f'void sub_{0x401000+i*0x80:08X}(void);' for i in range(len(cases)))+'\n'
    source+='\n'.join(generated)
    source+='''
__attribute__((section(".text.entry"))) void probe(State *ctx,uint32_t id) {
active=ctx; g_xbox_mem_offset=(ptrdiff_t)ctx->memory-0x200000;
switch(id) {
'''+''.join(f'case {i}: sub_{0x401000+i*0x80:08X}(); break;\n' for i in range(len(cases)))+'default:break;}}\n'
    (output/'probe.c').write_text(source)
    layout=output/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    flags=['-std=gnu11','-O2','-fno-strict-aliasing','-ffp-contract=off','-mearly-ra=none',
           '-ffreestanding','-fno-stack-protector','-nostdlib','-Werror=implicit-function-declaration']
    built=subprocess.run([str(compiler.resolve()),*flags,'-I',str(pc/'native'),'-I',str(sdk.resolve()/'templates/runtime'),
        '-Wl,-T,'+str(layout.resolve()),str((output/'probe.c').resolve()),'-o',str((output/'probe.elf').resolve())],capture_output=True,text=True)
    (output/'compiler.log').write_text(built.stdout+built.stderr)
    if built.returncode:raise RuntimeError(built.stderr)
    subprocess.run([str(compiler.with_name('aarch64-none-elf-objcopy.exe').resolve()),'-O','binary',str(output/'probe.elf'),str(output/'probe.bin')],check=True)
    arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x100000);arm.mem_write(0x100000,(output/'probe.bin').read_bytes())
    arm.mem_map(0x800000,0x10000)
    x86=Uc(UC_ARCH_X86,UC_MODE_32);x86.mem_map(0x401000,0x1000);x86.mem_map(0x200000,0x1000)
    regs=[getattr(xr,'UC_X86_REG_'+r) for r in ('EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI')]
    rng=random.Random(0xCFCF2026);count=0
    edge=[0,1,2,127,128,255,32767,32768,65535,0x7fffffff,0x80000000,0xffffffff]
    for number,code in enumerate(cases):
        address=0x401000+number*0x80;x86.mem_write(address,code);x86.ctl_remove_cache(0x401000,0x402000)
        vectors=[(a,b) for a in edge for b in edge]+[(rng.getrandbits(32),rng.getrandbits(32)) for _ in range(256)]
        for a,b in vectors:
            initial=[rng.getrandbits(32) for _ in range(8)];initial[0]=a;initial[2]=b
            initial[4]=0x200040
            if number==5:initial[1]=1+rng.randrange(5)  # bounded loop
            memory=bytearray(rng.randbytes(128));struct.pack_into('<I',memory,64,0x401f00)
            for r,v in zip(regs,initial):x86.reg_write(r,v)
            x86.reg_write(xr.UC_X86_REG_EFLAGS,0x202)
            x86.mem_write(0x200000,bytes(memory));x86.emu_start(address,0x401f00,count=200)
            assert x86.reg_read(xr.UC_X86_REG_EIP)==0x401f00
            state=struct.pack('<8I',*initial)+memory+struct.pack('<I',initial[5])
            arm.mem_write(0x800100,bytes(state));arm.reg_write(ar.UC_ARM64_REG_X0,0x800100)
            arm.reg_write(ar.UC_ARM64_REG_X1,number);arm.reg_write(ar.UC_ARM64_REG_SP,0x80f000)
            arm.reg_write(ar.UC_ARM64_REG_LR,0x1ff000);arm.emu_start(0x100000,0x1ff000,count=10000)
            actual=bytes(arm.mem_read(0x800100,164))
            expected=struct.pack('<8I',*(x86.reg_read(r) for r in regs))+bytes(x86.mem_read(0x200000,128))
            assert actual[:160]==expected,(number,hex(a),hex(b),actual[:32].hex(),expected[:32].hex())
            assert arm.reg_read(ar.UC_ARM64_REG_PC)==0x1ff000
            count+=1
    assert sdk_identity(sdk)==before_sdk
    assert identity=={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in own}
    result=dict(status='PASS',functions=len(cases),differential_cases=count,mixed_unknown_producer_kept_unimplemented=True,
        compiler_flags=flags,sdk_sha256=before_sdk,source_sha256=identity,native_engine_started=False)
    (output/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,required=True);p.add_argument('--python-deps',type=Path,required=True)
    p.add_argument('--cc',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();test(a.sdk,a.python_deps,a.cc,a.output)

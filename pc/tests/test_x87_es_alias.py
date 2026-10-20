"""Actual profile-emitted ES FADD vs 32-bit Windows, including pointer metadata."""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess
import sys


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    sys.path[:0]=[str(pc/'tools'),str(a.sdk.resolve()),str(a.python_deps.resolve())]
    from run_aot_analysis import sha
    from aot_x87_codegen import load_profile
    from aot_x87_extended import install,install_environment
    from tools.recomp.lifter import Lifter
    from tools.recomp.disasm import Disassembler
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
    import unicorn.arm64_const as ar
    core=json.loads(a.core.read_text());cc=core['compiler']
    if core['status']!='COMPILED' or not core['switch_abi']:raise ValueError('Require actual Switch core')
    frozen={Path(k):v for k,v in core['source_sha256'].items()}
    frozen.update({Path(o['object']):o['sha256'] for o in core['objects']})
    inputs=[Path(__file__).resolve(),pc/'tests/x87_es_oracle.c',pc/'tools/aot_x87_extended.py',pc/'tools/aot_x87_codegen.py',a.core.resolve()]
    frozen.update({p:sha(p) for p in inputs})
    if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Stale source/core input')
    def run(args,log):
        r=subprocess.run([str(v) for v in args],capture_output=True,text=True)
        (out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr[-3000:])
        return r.stdout
    oracle=out/'oracle.exe'
    run([*a.host_cc,'-target','x86-windows-gnu','-O2',pc/'tests/x87_es_oracle.c','-o',oracle],'oracle-compile.log')
    rng=random.Random(0x87326);pairs=[(0,0),(0x80000000,0),(0x3f800000,0xbf800000),(0x3f800000,0x33800000)]
    for unused in range(100):
        def operand():return (rng.randrange(2)<<31)|(rng.randrange(1,255)<<23)|rng.randrange(1<<23)
        pairs.append((operand(),operand()))
    records=[struct.pack('<IIHH',x,y,0x7f|(precision<<8)|(rc<<10),0)
             for x,y in pairs for precision in (0,2,3) for rc in range(4)]
    native=subprocess.run([str(oracle)],input=b''.join(records),capture_output=True)
    (out/'oracle.log').write_bytes(native.stderr)
    if native.returncode or len(native.stdout)!=len(records)*16:raise RuntimeError('32-bit selector oracle failed')
    ds,es=struct.unpack_from('<HH',native.stdout,12)
    if ds!=es:raise ValueError('The measured process does not satisfy the optional alias contract')
    rsp=out/'core.rsp';rsp.write_text('\n'.join('"'+str(o['object']).replace('\\','/')+'"' for o in core['objects'])+'\n')
    layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    install();base=Lifter.lift_instruction;decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True;dis=Disassembler()
    negative=0;cases=0;c1_differences=0
    for exception_only in (False,True):
        profile=dict(schema='fnv-x87-protected32-v3',contract_status='experimental',code_selector=35,data_selector=ds,
                     stack_selector=ds,extra_selector=es,opcode_compatibility=True,data_pointer_on_exception_only=exception_only)
        pp=out/('profile-'+str(int(exception_only))+'.json');pp.write_text(json.dumps(profile));load_profile(pp)
        for value in (True,-1,65536,ds^8):
            bad=dict(profile,extra_selector=value);bp=out/'bad-profile.json';bp.write_text(json.dumps(bad))
            try:load_profile(bp)
            except ValueError:negative+=1
            else:raise AssertionError('Invalid ES contract accepted')
        Lifter.lift_instruction=base
        install_environment(**{k:profile[k] for k in ('code_selector','data_selector','stack_selector','extra_selector','opcode_compatibility','data_pointer_on_exception_only')})
        lifter=Lifter();body=[]
        # FLD [ECX], FADD ES:[EAX], FNSTENV [EDI], FSTP tbyte [ESI].
        for i in decoder.disasm(bytes.fromhex('d90126d800d937db3e'),0x401000):body+=lifter.lift_instruction(dis._decode_instruction(i))
        if lifter.unimplemented:raise AssertionError('Explicit ES alias still rejected')
        source=out/('probe-'+str(int(exception_only))+'.c')
        text='''#include "x87_stack.h"
#include <stddef.h>
void *memcpy(void *d,const void *s,size_t n){unsigned char *a=d;const unsigned char *b=s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;}
void *memset(void *d,int c,size_t n){unsigned char *a=d;for(size_t i=0;i<n;i++)a[i]=(unsigned char)c;return d;}
__attribute__((section(".text.entry"))) int probe(const unsigned char *request,unsigned char *out){
struct fnv_x87_stack fnv_x87_state;fnv_x87_stack_init(&fnv_x87_state);fnv_x87_state.control=(uint16_t)(request[8]|request[9]<<8);
struct fnv_x87_environment fnv_x87_environment_state={0x12345678,0x10203040,0x3f,0x47,0x345};
ptrdiff_t g_xbox_mem_offset=(ptrdiff_t)request-0x200000;
uint32_t ecx=0x200000,eax=0x200004,edi=(uint32_t)(out-request+0x200000),esi=edi+28;
#define RECOMP_UNIMPL(text,va) return 10
BODY
return 0;}
'''.replace('BODY','\n'.join(body));source.write_text(text)
        elf=source.with_suffix('.elf');binary=source.with_suffix('.bin')
        run([*cc,'-O1','-fno-builtin','-ffreestanding','-fno-stack-protector','-nostdlib','-I'+str(pc/'native'),source,'@'+str(rsp),'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-o',elf],source.stem+'.log')
        run([Path(cc[0]).with_name('aarch64-none-elf-objcopy.exe'),'-O','binary',elf,binary],source.stem+'-objcopy.log')
        uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM);uc.mem_map(0x100000,0x200000);uc.mem_write(0x100000,binary.read_bytes());uc.mem_map(0x800000,0x10000)
        for index,record in enumerate(records):
            uc.mem_write(0x800100,record);uc.mem_write(0x800400,b'\xa5'*48)
            for reg,v in ((ar.UC_ARM64_REG_X0,0x800100),(ar.UC_ARM64_REG_X1,0x800400),(ar.UC_ARM64_REG_SP,0x80f000),(ar.UC_ARM64_REG_LR,0x2ff000)):uc.reg_write(reg,v)
            uc.emu_start(0x100000,0x2ff000,count=50000)
            if uc.reg_read(ar.UC_ARM64_REG_PC)!=0x2ff000 or uc.reg_read(ar.UC_ARM64_REG_X0)!=0:raise AssertionError('ARM execution failed')
            actual=bytes(uc.mem_read(0x800400,48));expected=native.stdout[index*16:index*16+16]
            status_difference=struct.unpack_from('<H',actual,4)[0]^struct.unpack_from('<H',expected,10)[0]
            if actual[28:38]!=expected[:10] or status_difference&~0x200:raise AssertionError(('Native numeric/status mismatch',index,exception_only))
            # The existing arithmetic core does not reconstruct rounded-up C1.
            # Keep that separate, visible gap; ES support must not hide it.
            c1_differences+=bool(status_difference&0x200)
            if struct.unpack_from('<I',actual,12)[0]!=0x401002:raise AssertionError('Wrong FIP')
            if struct.unpack_from('<I',actual,20)[0]!=(0x10203040 if exception_only else 0x200004):raise AssertionError('Wrong FDP')
            if struct.unpack_from('<H',actual,24)[0]!=(0x47 if exception_only else es):raise AssertionError('Wrong ES data selector')
            if actual[38:]!=b'\xa5'*10:raise AssertionError('Output guard corrupted')
            cases+=1
    Lifter.lift_instruction=base
    if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Source/core changed during test')
    proof=dict(status='ES_ALIAS_VERIFIED_WITH_ARITHMETIC_C1_GAP' if c1_differences else 'PASS',
               native_x86_bits=32,native_ds=ds,native_es=es,numeric_and_exception_status_comparisons=cases,
               existing_arithmetic_c1_differences=c1_differences,full_arithmetic_c1_matches_native=not c1_differences,
               precision_modes=3,rounding_modes=4,data_pointer_policies=2,negative_profile_checks=negative,
               source_sha256={str(p):h for p,h in frozen.items()},game_executed=False,hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(proof,indent=2)+'\n');print(json.dumps({k:v for k,v in proof.items() if k!='source_sha256'},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('core','sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--host-cc',nargs='+',required=True);test(p.parse_args())

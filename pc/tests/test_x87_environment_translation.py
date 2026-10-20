"""Translate synthetic environment instructions and check native-x87 image vectors.

Separate metadata checks use explicit legacy/modern FOP/FDP policies. They do
not assert that host CPUID equals the eventual guest CPU contract.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
from test_x87_environment import test as environment_oracle, verify, sha


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    environment_oracle(SimpleNamespace(build=a.build,arm_build=a.arm_build,python_deps=a.python_deps,output=out/'native-vectors'))
    arm=json.loads(a.arm_build.read_text());verify(arm)
    sys.path[:0]=[str(a.sdk.resolve()),str(a.python_deps.resolve()),str(pc/'tools')]
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    from tools.recomp.disasm import Disassembler
    from tools.recomp.lifter import Lifter
    from aot_frame_restore import install as frame
    from aot_x87_extended import install,install_environment
    from run_aot_analysis import sdk_identity
    from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
    import unicorn.arm64_const as ar
    frame();install();base_lift=Lifter.lift_instruction
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True;disassembler=Disassembler()
    sources=[];checks=0;negative=0
    for compat in (False,True):
      for exception_only in (False,True):
        Lifter.lift_instruction=base_lift
        install_environment(code_selector=0x23,data_selector=0x2b,stack_selector=0x33,
                            opcode_compatibility=compat,data_pointer_on_exception_only=exception_only)
        try:
            install_environment(code_selector=0x23,data_selector=0x2b,stack_selector=0x33,
                                opcode_compatibility=compat,data_pointer_on_exception_only=exception_only)
            raise AssertionError('Duplicate environment install accepted')
        except ValueError:negative+=1
        def lift(raw,va):
            lifter=Lifter();lines=[]
            for decoded in decoder.disasm(raw,va):lines+=lifter.lift_instruction(disassembler._decode_instruction(decoded))
            assert not lifter.unimplemented
            return '\n'.join(lines)
        cases=[]
        for mode in range(4):
            raw=b'\xdd\x27'
            if mode==2:raw+=b'\xd9\x67\x6c'
            raw+=b'\xd9\x36' if mode in (1,3) else b'\xdd\x36'
            if mode==3:raw+=b'\xd9\x26'
            raw+=b'\xdd\x76\x6c'
            cases.append(f'case {mode}: {{'+lift(raw,0x401000+mode*0x100)+'break;}')
        # Prefix inspection must reject malformed width information supplied
        # by Capstone for 16-bit state images. Reject address-size/FS/LOCK too.
        for raw in (b'\x66\xd9\x37',b'\x66\xdd\x37',b'\x66\xdd\x27',b'\x66\xd9\x27',
                    b'\x67\xd9\x37',b'\x64\xdb\x2f'):
            lifter=Lifter();decoded=next(decoder.disasm(raw,0x402000));ins=disassembler._decode_instruction(decoded)
            lines=lifter.lift_instruction(ins)
            assert lifter.unimplemented and not any('stack_save32' in line or 'stack_restore32' in line for line in lines)
            negative+=1
        numeric=[
            (b'\xd9\xe8\xd9\x37',0x403000,None,None),
            (b'\xd9\xe8\xd9\xd0\xd9\x37',0x403002,None,None),
            (b'\xd9\xe8\x9b\xd9\x37',0x403000,None,None),
            (b'\xdb\x6f\x1c\xd9\x37',0x403000,0x20001c,0x2b),
            (b'\x36\xdb\x6f\x1c\xd9\x37',0x403000,0x20001c,0x33),
            (b'\xdb\x6d\x1c\xd9\x37',0x403000,0x20001c,0x33),
            (b'\x3e\xdb\x6d\x1c\xd9\x37',0x403000,0x20001c,0x2b),
            (b'\x66\xd9\xe8\xd9\x37',0x403000,None,None),
        ]
        metadata=[]
        for index,(raw,expected_ip,expected_dp,expected_ds) in enumerate(numeric):
            instructions=list(decoder.disasm(raw,0x403000));last=[i for i in instructions if i.mnemonic not in ('fnstenv','wait')][-1]
            b=bytes(last.bytes);offset=0
            while b[offset] not in range(0xd8,0xe0):offset+=1
            expected_opcode=((b[offset]&7)<<8)|b[offset+1] if compat else 0x345
            dp=expected_dp if expected_dp is not None and not exception_only else 0x10203040
            ds=expected_ds if expected_dp is not None and not exception_only else 0x47
            # Save to [edi] uses the request buffer, so this scratch path sets
            # edi to the output address except during explicit memory loads.
            # Redirect only the final FNSTENV operand to [esi].
            raw=raw[:-2]+b'\xd9\x36'
            body=lift(raw,0x403000)
            metadata.append(f'case {index}: {{'+body+f'if(fnv_x87_environment_state.instruction_pointer!=0x{expected_ip:X}u || fnv_x87_environment_state.code_selector!=0x23 || fnv_x87_environment_state.opcode!=0x{expected_opcode:X} || fnv_x87_environment_state.data_pointer!=0x{dp:X}u || fnv_x87_environment_state.data_selector!=0x{ds:X}) return 11; break;}}')
            checks+=1
        # Control instructions must retain previous pointers; FNINIT clears them.
        controls=[b'\xd9\x2f',b'\xd9\x3e',b'\xdd\x3e',b'\xdf\xe0',b'\xdb\xe2',b'\x9b']
        for raw in controls:
            code=lift(raw,0x404000)
            assert 'environment_state.' not in code and 'environment_state=' not in code
            checks+=1
        assert 'environment_state=(struct fnv_x87_environment){0}' in lift(b'\xdb\xe3',0x404000);checks+=1
        text=r'''#include "x87_stack.h"
#include <stddef.h>
void *memcpy(void *to,const void *from,size_t n){unsigned char *d=to;const unsigned char *s=from;for(size_t i=0;i<n;i++)d[i]=s[i];return to;}
void *memset(void *to,int v,size_t n){unsigned char *d=to;for(size_t i=0;i<n;i++)d[i]=(unsigned char)v;return to;}
__attribute__((section(".text.entry"))) int probe(const unsigned char *request,unsigned char *out,unsigned numeric) {
struct fnv_x87_stack fnv_x87_state;fnv_x87_stack_init(&fnv_x87_state);
struct fnv_x87_environment fnv_x87_environment_state={0x12345678,0x10203040,0x3f,0x47,0x345};
ptrdiff_t g_xbox_mem_offset=(ptrdiff_t)request-0x200000;
uint32_t edi=0x200000,ebp=0x200000,esi=(uint32_t)(out-request+0x200000),eax=0xabcdef00;
#define RECOMP_UNIMPL(text,va) return 10
for(unsigned i=0;i<216;i++)out[i]=0;
if(numeric){switch(numeric-1){METADATA}return 0;}
switch(request[136]){CASES}return 0;
}
'''
        text=text.replace('METADATA','\n'.join(metadata)).replace('CASES','\n'.join(cases))
        name=f'profile-{int(compat)}-{int(exception_only)}';source=out/(name+'.c');source.write_text(text);sources.append(source)
        layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
        rsp=out/'arm.rsp';rsp.write_text('\n'.join('"'+o['object'].replace('\\','/')+'"' for o in arm['objects'])+'\n')
        cc=arm['compiler'];elf=out/(name+'.elf')
        r=subprocess.run([*cc,'-O2','-fno-builtin','-ffreestanding','-fno-stack-protector','-nostdlib','-I'+str(pc/'native'),str(source),'@'+str(rsp),'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-o',str(elf)],capture_output=True,text=True)
        (out/(name+'-compiler.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr[-3000:])
        binary=out/(name+'.bin');subprocess.run([str(Path(cc[0]).resolve().with_name('aarch64-none-elf-objcopy.exe')),'-O','binary',str(elf),str(binary)],check=True)
        uc=Uc(UC_ARCH_ARM64,UC_MODE_ARM);uc.mem_map(0x100000,0x200000);uc.mem_write(0x100000,binary.read_bytes());uc.mem_map(0x800000,0x10000)
        data=(out/'native-vectors/inputs.bin').read_bytes();expected=(out/'native-vectors/expected.bin').read_bytes()
        def execute(record,mode=0):
            uc.mem_write(0x800100,bytes(record));uc.mem_write(0x800400,b'\xa5'*224)
            for reg,value in ((ar.UC_ARM64_REG_X0,0x800100),(ar.UC_ARM64_REG_X1,0x800400),(ar.UC_ARM64_REG_X2,mode),(ar.UC_ARM64_REG_SP,0x80f000),(ar.UC_ARM64_REG_LR,0x2ff000)):uc.reg_write(reg,value)
            uc.emu_start(0x100000,0x2ff000,count=30000)
            assert uc.reg_read(ar.UC_ARM64_REG_PC)==0x2ff000 and uc.reg_read(ar.UC_ARM64_REG_X0)==0,(name,mode)
            assert bytes(uc.mem_read(0x8004d8,8))==b'\xa5'*8
            return bytes(uc.mem_read(0x800400,216))
        for index in range(len(data)//137):assert execute(data[index*137:(index+1)*137])==expected[index*216:(index+1)*216],(name,index)
        # Seed a normal extended operand for memory metadata cases.
        record=bytearray(137);record[28:38]=bytes.fromhex('0000000000000080ff3f')
        for mode in range(1,len(numeric)+1):execute(record,mode)
    Lifter.lift_instruction=base_lift
    verify(arm)
    paths=[Path(__file__).resolve(),Path(__file__).with_name('test_x87_environment.py').resolve(),pc/'tools/aot_x87_extended.py',pc/'tools/aot_frame_restore.py',a.build.resolve(),a.arm_build.resolve(),out/'native-vectors/validation.json']
    result=dict(status='PASS',native_environment_vectors=len(data)//137,translated_environment_cases=4*(len(data)//137),
        metadata_policy_checks=checks,negative_checks=negative,profiles=4,source_sha256={str(p):sha(p) for p in paths},
        generated_synthetic_sources={str(p):sha(p) for p in sources},sdk_sha256=sdk_identity(a.sdk.resolve()),
        runtime_integrated=False,switch_hardware_tested=False,limits=['Metadata policy checks are architectural checks; host CPU is not used as a guest feature profile'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS',result['translated_environment_cases'],'translated native-oracle cases;',checks,'metadata checks')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','arm-build','sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    test(p.parse_args())

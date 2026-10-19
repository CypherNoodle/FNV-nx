"""F3 C3 near-return CFG and x86/ARM64 differential execution; no game code."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys


def test(a):
    pc=Path(__file__).resolve().parents[1];sdk=a.sdk.resolve();out=a.output.resolve()
    if out.exists():raise ValueError('Use fresh output')
    sys.path[:0]=[str(sdk),str(a.python_deps.resolve()),str(pc/'tools'),str(pc/'tests')]
    from pe_fixture import write_pe
    from prepare_aot_image import prepare
    from run_aot_analysis import sdk_identity,sha
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    from tools.recomp.disasm import Instruction
    from aot_frame_restore import install
    from unicorn import Uc,UC_ARCH_X86,UC_ARCH_ARM64,UC_MODE_32,UC_MODE_ARM
    import unicorn.x86_const as xr
    import unicorn.arm64_const as ar
    inputs=[Path(__file__).resolve(),pc/'tools/aot_frame_restore.py',pc/'tests/pe_fixture.py',pc/'tools/prepare_aot_image.py',pc/'tools/run_aot_analysis.py',a.cc.resolve(),a.cc.with_name('aarch64-none-elf-objcopy.exe').resolve(),
            *[p for d in (sdk/'templates/runtime',sdk/'src/kernel') for p in d.rglob('*.h')]]
    frozen={str(p):sha(p) for p in inputs};before=sdk_identity(sdk)
    if not a.without_overlay:install();install() # Idempotent.
    ret=Instruction(0x401000,2,'repz ret','','f3c3')
    assert ret.is_ret==ret.is_terminator==(not a.without_overlay)
    for code in ('c3','f2c3','f3c20000','66f3c3'):
        ins=Instruction(0x401000,len(bytes.fromhex(code)),'repz ret','',code)
        assert not ins.is_ret # Do not broaden support by mnemonic alone.
    cases={0x401000:bytes.fromhex('f3c3'),
        0x401040:bytes.fromhex('85c07402f3c3b878563412f3c3'),
        0x401080:bytes.fromhex('85c07405b801000000f3c3'),
        0x4010c0:b'\xe8'+struct.pack('<i',0x401100-0x4010c5)+bytes.fromhex('ba44332211f3c3'),
        0x401100:bytes.fromhex('b821436587f3c3'),
        0x401140:bytes.fromhex('5589e553565f5bc9f3c3'),
        0x401180:bytes.fromhex('c3')}
    out.mkdir(parents=True);exe=out/'fixture.exe';write_pe(exe);raw=bytearray(exe.read_bytes());struct.pack_into('<I',raw,0x98+224+36,0x60000020)
    functions={}
    for at,code in cases.items():
        offset=at-0x401000+0x200;raw[offset:offset+len(code)]=code
        functions[at]=dict(start=hex(at),end=at+len(code),size=len(code),name=f'sub_{at:08X}',section='.text',called_by=[])
    exe.write_bytes(raw);prepare(exe,out/'image',sha(exe));image=out/'image/fnv-analysis.xbe';config.configure_from_xbe(str(image))
    translator=FunctionTranslator(image.read_bytes(),functions,seh_prolog=0,seh_epilog=0)
    generated=[translator.translate_function(at,info) for at,info in functions.items()]
    if a.without_overlay:
        assert any('RECOMP_UNIMPL("repz ret"' in code for code in generated)
        result=dict(status='EXPECTED_UNSUPPORTED',generation_only=True,source_sha256=frozen,sdk_sha256=before)
    else:
        assert not any('RECOMP_UNIMPL' in code or 'translation failed' in code for code in generated)
        source='#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\n'
        source+='typedef struct { uint32_t gpr[8]; unsigned char memory[128]; uint32_t seh;} State;\n_Static_assert(sizeof(State)==164,"layout");\nptrdiff_t g_xbox_mem_offset;\nstatic State *active;\n'
        for i,reg in enumerate(('eax','ecx','edx','ebx','esp','ebp','esi','edi')):source+=f'#define g_{reg} (active->gpr[{i}])\n'
        source+='#define g_seh_ebp (active->seh)\n'+''.join(f'void sub_{at:08X}(void);\n' for at in functions)+''.join(generated)
        source+='__attribute__((section(".text.entry"))) void probe(State *state,uint32_t id){active=state;g_xbox_mem_offset=(ptrdiff_t)state->memory-0x200000;switch(id){'
        source+=''.join(f'case {i}:sub_{at:08X}();break;' for i,at in enumerate(functions))+'default:break;}}\n'
        (out/'probe.c').write_text(source);layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
        command=[str(a.cc.resolve()),'-std=gnu11','-O2','-fno-strict-aliasing','-ffp-contract=off','-mearly-ra=none','-ffreestanding','-fno-stack-protector','-nostdlib','-Werror=implicit-function-declaration','-I'+str(sdk/'templates/runtime'),'-Wl,-T,'+str(layout),str(out/'probe.c'),'-o',str(out/'probe.elf')]
        built=subprocess.run(command,capture_output=True,text=True,timeout=60);(out/'compiler.log').write_text(built.stdout+built.stderr)
        if built.returncode:raise RuntimeError(built.stderr)
        subprocess.run([str(a.cc.with_name('aarch64-none-elf-objcopy.exe').resolve()),'-O','binary',str(out/'probe.elf'),str(out/'probe.bin')],check=True,timeout=30)
        arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x100000);arm.mem_write(0x100000,(out/'probe.bin').read_bytes());arm.mem_map(0x800000,0x10000)
        x86=Uc(UC_ARCH_X86,UC_MODE_32);x86.mem_map(0x401000,0x1000);x86.mem_map(0x200000,0x1000)
        for at,code in cases.items():x86.mem_write(at,code)
        registers=[getattr(xr,'UC_X86_REG_'+reg) for reg in ('EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI')]
        rng=random.Random(0xf3c3);checked=0
        for case,at in enumerate(cases):
            for trial in range(1000):
                initial=[rng.getrandbits(32) for _ in range(8)];initial[4]=0x200040;initial[0]=0 if trial%2 else initial[0];initial[1]=0 if trial%3==0 else initial[1]
                memory=bytearray(rng.randbytes(128));struct.pack_into('<I',memory,64,0x401f00)
                for reg,value in zip(registers,initial):x86.reg_write(reg,value)
                x86.reg_write(xr.UC_X86_REG_EFLAGS,0x202);x86.mem_write(0x200000,bytes(memory));x86.emu_start(at,0x401f00,count=100)
                assert x86.reg_read(xr.UC_X86_REG_EIP)==0x401f00
                state=struct.pack('<8I',*initial)+memory+struct.pack('<I',initial[5]);arm.mem_write(0x800100,bytes(state))
                arm.reg_write(ar.UC_ARM64_REG_X0,0x800100);arm.reg_write(ar.UC_ARM64_REG_X1,case);arm.reg_write(ar.UC_ARM64_REG_SP,0x80f000);arm.reg_write(ar.UC_ARM64_REG_LR,0x1ff000)
                arm.emu_start(0x100000,0x1ff000,count=10000);actual=bytes(arm.mem_read(0x800100,164))
                expected=struct.pack('<8I',*(x86.reg_read(reg) for reg in registers))+bytes(x86.mem_read(0x200000,128))
                assert actual[:160]==expected,(case,trial,actual[:32].hex(),expected[:32].hex())
                assert arm.reg_read(ar.UC_ARM64_REG_PC)==0x1ff000;checked+=1
        result=dict(status='PASS',functions=len(cases),differential_cases=checked,emulated_x86_and_ARM64=True,switch_hardware_tested=False,
                    unrelated_encodings_unchanged=True,sdk_sha256=before,source_sha256=frozen,generated_sha256=hashlib.sha256(source.encode()).hexdigest())
    assert sdk_identity(sdk)==before
    for name,digest in frozen.items():assert sha(Path(name))==digest,name
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'},indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output','cc'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--without-overlay',action='store_true');test(p.parse_args())

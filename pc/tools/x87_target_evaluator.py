"""Execute a bounded math request with the exact compiled ARM64 model.

Only the standalone target math core is emulated. This is not a guest CPU
backend, host-libm substitute, engine boot or hardware performance test.
Captured requests and results must remain private.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def unchanged(items):
    for name,digest in items.items():
        if sha(Path(name))!=digest:raise ValueError('Changed evaluator input: '+name)


def build(core_path,output):
    if output.exists():raise ValueError('Fresh output required')
    core_path=core_path.resolve();core=json.loads(core_path.read_text())
    from x87_models import TRIG_MODEL
    if (core.get('status')!='COMPILED' or core.get('switch_abi') is not True or
        core.get('transcendental_model')!=TRIG_MODEL or len(core.get('objects',[]))!=101):
        raise ValueError('Require the complete production Switch trig/math core')
    unchanged(core['source_sha256'])
    cc=core['compiler'];compiler=Path(cc[0]);pc=Path(__file__).resolve().parents[1]
    objects=[Path(item['object']) for item in core['objects']]
    if len(set(objects))!=101:raise ValueError('Duplicate arithmetic object')
    for item in core['objects']:
        if sha(Path(item['object']))!=item['sha256']:raise ValueError('Changed arithmetic object')
    output=output.resolve();output.mkdir(parents=True)
    def run(args,name):
        r=subprocess.run([str(x) for x in args],capture_output=True,text=True,timeout=120)
        (output/name).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+': '+r.stderr[-4000:])
        return r.stdout
    if run([*cc,'-dumpmachine'],'machine.log').strip()!='aarch64-none-elf':
        raise ValueError('Unexpected target compiler')
    frozen={**core['source_sha256'],str(core_path):sha(core_path),str(Path(__file__).resolve()):sha(Path(__file__))}
    sources=[pc/'tests/startup_math_arm64_entry.c',pc/'tests/startup_math_state.h']
    for p in sources:frozen[str(p)]=sha(p)
    for item in core['objects']:frozen[item['object']]=item['sha256']
    libraries=[]
    for name in ('libm.a','libgcc.a'):
        p=Path(run([*cc,'-print-file-name='+name],name+'.log').strip()).resolve()
        if not p.is_file():raise ValueError('Missing target library '+name)
        libraries.append(p);frozen[str(p)]=sha(p)
    layout=output/'layout.ld'
    layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    rsp=output/'objects.rsp';rsp.write_text('\n'.join('"'+p.as_posix()+'"' for p in objects)+'\n')
    elf=output/'probe.elf';binary=output/'probe.bin'
    run([*cc,*core['flags'],'-ffreestanding','-fno-stack-protector','-nostdlib',
         '-I'+str(pc/'tests'),sources[0],'@'+str(rsp),'-Wl,--gc-sections','-Wl,-T,'+str(layout),
         '-lm','-lgcc','-o',elf],'link.log')
    objcopy=compiler.with_name('aarch64-none-elf-objcopy.exe');nm=compiler.with_name('aarch64-none-elf-nm.exe')
    for p in (objcopy,nm):frozen[str(p)]=sha(p)
    symbols=run([nm,elf],'symbols.log')
    if not re.search(r'^0000000000100000 T probe$',symbols,re.M):raise ValueError('Unexpected target entry')
    run([objcopy,'-O','binary',elf,binary],'objcopy.log')
    if not 0<len(binary.read_bytes())<0x2ff000:raise ValueError('Unexpected target image size')
    frozen[str(layout)]=sha(layout);frozen[str(rsp)]=sha(rsp)
    unchanged(frozen)
    report=dict(status='ARM64_MATH_EVALUATOR_BUILT',schema='fnv-target-math-evaluator-v1',
                model=TRIG_MODEL,core_validation_sha256=sha(core_path),source_sha256=frozen,
                generated_sha256={p.name:sha(p) for p in (elf,binary)},entry=0x100000,
                input_bytes=88,output_bytes=86,instruction_budget=2000000,
                engine_started=False,hardware_tested=False)
    (output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print('BUILT exact ARM64 math evaluator, 101 verified production core objects')
    return report


class Evaluator:
    def __init__(self,path,deps):
        self.path=path.resolve();self.report=json.loads(self.path.read_text());r=self.report
        if (r.get('status')!='ARM64_MATH_EVALUATOR_BUILT' or r.get('schema')!='fnv-target-math-evaluator-v1' or
            r.get('input_bytes')!=88 or r.get('output_bytes')!=86 or r.get('entry')!=0x100000 or
            r.get('instruction_budget')!=2000000):raise ValueError('Invalid target evaluator proof')
        unchanged(r['source_sha256'])
        for name,digest in r['generated_sha256'].items():
            if Path(name).name!=name or sha(self.path.parent/name)!=digest:raise ValueError('Changed target image')
        self.binary=(self.path.parent/'probe.bin').read_bytes()
        sys.path.insert(0,str(deps.resolve()))
        from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
        import unicorn.arm64_const as ar
        self.Uc=Uc;self.arch=UC_ARCH_ARM64;self.mode=UC_MODE_ARM;self.ar=ar

    def evaluate(self,request):
        if len(request)!=88:raise ValueError('Require exactly one canonical target-math request')
        arm=self.Uc(self.arch,self.mode);ar=self.ar
        arm.mem_map(0x100000,0x300000);arm.mem_write(0x100000,self.binary)
        arm.mem_map(0x800000,0x10000);arm.mem_write(0x800100,request)
        arm.mem_write(0x800200,b'\xa5'*102)
        fpcr=2<<22;fpsr=0x11
        for reg,value in ((ar.UC_ARM64_REG_X0,0x800100),(ar.UC_ARM64_REG_X1,0x800208),
                          (ar.UC_ARM64_REG_SP,0x80f000),(ar.UC_ARM64_REG_LR,0x3ff000),
                          (ar.UC_ARM64_REG_FPCR,fpcr),(ar.UC_ARM64_REG_FPSR,fpsr)):
            arm.reg_write(reg,value)
        arm.emu_start(0x100000,0x3ff000,count=2000000)
        if arm.reg_read(ar.UC_ARM64_REG_PC)!=0x3ff000:raise ValueError('Target math instruction budget exceeded')
        if arm.reg_read(ar.UC_ARM64_REG_FPCR)!=fpcr or arm.reg_read(ar.UC_ARM64_REG_FPSR)!=fpsr:
            raise ValueError('Target math changed caller floating-point state')
        result=arm.reg_read(ar.UC_ARM64_REG_X0);data=bytes(arm.mem_read(0x800200,102))
        if data[:8]!=b'\xa5'*8 or data[94:]!=b'\xa5'*8:raise ValueError('Target output guard overwritten')
        state=data[8:94]
        if result>4:raise ValueError('Unknown target math result')
        if result and state!=request[2:]:raise ValueError('Rejected target request changed x87 state')
        return result,state


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='command',required=True)
    b=sub.add_parser('build');b.add_argument('--core',type=Path,required=True);b.add_argument('--output',type=Path,required=True)
    a=p.parse_args();build(a.core,a.output)

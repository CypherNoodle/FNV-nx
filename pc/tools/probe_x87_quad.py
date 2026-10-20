"""Research target binary128 libm versus native x87; never enables translation.

Only synthetic numeric inputs are used. Runtime faults or differing payloads
are findings, not accepted approximations. Domain/flags/stack compatibility
must be implemented and verified separately before a production backend.
"""
import argparse
from decimal import Decimal,localcontext
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def rounded_ext80(data):
    lo,hi=struct.unpack('<QQ',data);exponent=(hi>>48)&0x7fff;sign=(hi>>48)&0x8000
    sig=((hi&0xffffffffffff)<<15)|(lo>>49)
    if exponent:sig|=1<<63
    lost=lo&((1<<49)-1)
    if exponent==0x7fff:
        if (hi&0xffffffffffff or lo) and sig==1<<63:sig|=1
    else:
        sig+=int(lost>(1<<48) or (lost==(1<<48) and sig&1))
        if sig==1<<64:sig=1<<63;exponent+=1
        if not exponent and sig>>63:exponent=1
    return struct.pack('<QH',sig,sign|exponent)


def key(data):
    sig,se=struct.unpack('<QH',data);exponent=se&0x7fff
    if exponent==0x7fff:return None
    value=((exponent-1)<<63)+sig if exponent else sig
    return -value if se&0x8000 else value


def decimal_value(data):
    sig,se=struct.unpack('<QH',data);exponent=se&0x7fff
    return Decimal(-sig if se&0x8000 else sig)*(Decimal(2)**((exponent or 1)-0x3fff-63))


def probe(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True);pc=Path(__file__).resolve().parents[1]
    source=pc/'tests/x87_quad_probe.c';oracle=pc/'tests/x87_quad_oracle.c'
    frozen={str(p):sha(p) for p in (Path(__file__).resolve(),source,oracle,a.cc.resolve(),Path(a.host_cc[0]).resolve())}
    def run(command,log):
        r=subprocess.run([str(x) for x in command],capture_output=True,text=True);(out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr[-3000:] or str(r.returncode))
        return r.stdout.strip()
    libraries=[]
    for library in ('libm.a','libgcc.a'):
        path=Path(run([a.cc,'-print-file-name='+library],library+'-path.log')).resolve()
        if not path.is_file():raise ValueError('Missing target library '+str(path))
        libraries.append(dict(path=str(path),sha256=sha(path)))
    layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    elf=out/'probe.elf';binary=out/'probe.bin';exe=out/'oracle.exe'
    run([a.cc,'-std=c11','-O2','-ffreestanding','-fno-stack-protector','-nostdlib',source,'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-lm','-lgcc','-o',elf],'target-compiler.log')
    run([a.cc.resolve().with_name('aarch64-none-elf-objcopy.exe'),'-O','binary',elf,binary],'objcopy.log')
    run([*a.host_cc,'-O2',oracle,'-o',exe],'host-compiler.log')
    def raw(sig,exp):return struct.pack('<QH',sig,exp)
    def dyadic(n,scale=0):
        if not n:return bytes(10)
        bits=abs(n).bit_length();assert bits<=64
        return raw(abs(n)<<(64-bits),(0x8000 if n<0 else 0)|((bits-1-scale)+0x3fff))
    angles=[dyadic(n,s) for n,s in ((0,0),(1,0),(-1,0),(1,1),(-1,1),(1000,0),(10**10,0),(10**18,0),(1<<62,0),(1,100))]
    angles.extend([raw(0xc90fdaa22168c235,0x4000),raw(0xcccccccccccccccd,0x3ffb)])
    cases=[(op,x,dyadic(1)) for op in range(3) for x in angles]
    cases.extend((3,dyadic(y),dyadic(x)) for y,x in ((1,1),(1,-1),(-1,1),(-1,-1),(0,1),(1,0)))
    cases.extend((4,dyadic(x),dyadic(y)) for x,y in ((1,2),(2,3),(3,2),(10,-3)))
    cases.extend((5,dyadic(n,s),dyadic(1)) for n,s in ((-1,0),(0,0),(1,0),(1,1),(-1,1),(1,100)))
    records=[x+y+bytes([op,0,0,0]) for op,x,y in cases];data=b''.join(records);(out/'inputs.bin').write_bytes(data)
    native=subprocess.run([str(exe)],input=data,capture_output=True);(out/'native.log').write_bytes(native.stderr)
    if native.returncode or len(native.stdout)!=12*len(records):raise RuntimeError('Native oracle failed')
    (out/'native-results.bin').write_bytes(native.stdout)
    sys.path.insert(0,str(a.python_deps.resolve()))
    from unicorn import Uc,UcError,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_MEM_INVALID
    import unicorn.arm64_const as ar
    arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x200000);arm.mem_write(0x100000,binary.read_bytes());arm.mem_map(0x800000,0x10000)
    faults=[]
    def memory_fault(uc,kind,address,size,value,user):
        faults.append(dict(address=address,pc=uc.reg_read(ar.UC_ARM64_REG_PC),access=kind,size=size));return False
    arm.hook_add(UC_HOOK_MEM_INVALID,memory_fault);results=[]
    for index,record in enumerate(records):
        arm.mem_write(0x800100,record);arm.mem_write(0x800200,b'\xa5'*24)
        arm.reg_write(ar.UC_ARM64_REG_X0,0x800100);arm.reg_write(ar.UC_ARM64_REG_X1,0x800200);arm.reg_write(ar.UC_ARM64_REG_SP,0x80f000);arm.reg_write(ar.UC_ARM64_REG_LR,0x2ff000);arm.reg_write(ar.UC_ARM64_REG_FPCR,0)
        case=dict(op=record[20],input=record[:20].hex())
        try:arm.emu_start(0x100000,0x2ff000,count=2000000)
        except UcError as error:
            results.append(dict(**case,error=str(error),fault=faults[-1] if faults else None));continue
        if arm.reg_read(ar.UC_ARM64_REG_PC)!=0x2ff000:
            results.append(dict(**case,error='Instruction budget exhausted'));continue
        payload=bytes(arm.mem_read(0x800200,24));target=rounded_ext80(payload[:16]);expected=native.stdout[index*12:index*12+10]
        k1,k2=key(target),key(expected)
        with localcontext() as context:
            context.prec=60
            absolute=str(abs(decimal_value(target)-decimal_value(expected))) if k1 is not None and k2 is not None else None
        results.append(dict(**case,native=expected.hex(),quad_rounded=target.hex(),ulp_distance=abs(k1-k2) if k1 is not None and k2 is not None else None,
            absolute_error_decimal=absolute,errno=struct.unpack('<I',payload[16:20])[0],fpsr=struct.unpack('<I',payload[20:24])[0]))
    for path,digest in frozen.items():assert sha(Path(path))==digest
    for library in libraries:assert sha(Path(library['path']))==library['sha256']
    names=('sin','cos','tan','atan2','y_log2_x','2_pow_x_minus_1');summary={}
    for op,name in enumerate(names):
        rows=[r for r in results if r['op']==op];valid=[r for r in rows if 'error' not in r]
        summary[name]=dict(cases=len(rows),runtime_faults=len(rows)-len(valid),exact_payloads=sum(r['ulp_distance']==0 for r in valid),max_ulp=max((r['ulp_distance'] for r in valid if r['ulp_distance'] is not None),default=None))
    errors=sum('error' in r for r in results)
    result=dict(status='RESEARCH_WITH_RUNTIME_FAULTS' if errors else 'RESEARCH_ONLY_NOT_A_TRANSLATION',native_cases=len(records),runtime_faults=errors,
        source_sha256=frozen,libraries=libraries,target_elf_sha256=sha(elf),summary=summary,results=results,translation_enabled=False,switch_hardware_tested=False,
        limits=['Nearest rounding only','Raw payload comparisons do not validate domains, x87 flags or stack changes','Different x87 argument reduction may produce different payloads even for accurate mathematical functions'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(status=result['status'],summary=summary),indent=2))
    return 2 if errors else 0


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('cc','python-deps','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--host-cc',nargs='+',required=True)
    raise SystemExit(probe(parser.parse_args()))

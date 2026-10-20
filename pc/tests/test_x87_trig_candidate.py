"""Real-profile ARM64 trig: native stack/flags and independent true-pi values."""
import argparse
from collections import Counter
from decimal import Decimal,localcontext
import hashlib
import json
from pathlib import Path
import random
import re
import struct
import subprocess
import sys


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True);pc=Path(__file__).resolve().parents[1]
    sys.path[:0]=[str(pc/'tools'),str(a.python_deps.resolve()),str(a.sdk.resolve())]
    from x87_models import TRIG_MODEL
    from aot_x87_codegen import load_profile,install_profile
    from run_aot_analysis import sdk_identity
    from aot_frame_restore import install as frame
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    from tools.recomp.disasm import Disassembler
    from tools.recomp.lifter import Lifter
    from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM
    import unicorn.arm64_const as ar
    from x87_trig_decimal import reference
    from x87_transcendental_decimal import atan_series,round_normal
    core=json.loads(a.core.read_text());profile=load_profile(a.profile)
    if core['status']!='COMPILED' or not core['switch_abi'] or core.get('transcendental_model')!=TRIG_MODEL or profile['transcendental_model']!=TRIG_MODEL:raise ValueError('Matching real Switch trig core/profile required')
    frozen=core['source_sha256'].copy();paths=[Path(__file__).resolve(),pc/'tests/x87_trig_oracle.c',pc/'tools/x87_trig_decimal.py',pc/'tools/x87_transcendental_decimal.py',pc/'tools/aot_x87_codegen.py',pc/'tools/aot_x87_extended.py',pc/'tools/aot_sahf_flags.py',pc/'tools/aot_frame_restore.py',a.profile.resolve(),a.core.resolve(),Path(a.host_cc[0]).resolve()]
    frozen.update({str(p):sha(p) for p in paths})
    for p,h in frozen.items():
        if sha(Path(p))!=h:raise ValueError('Changed dependency '+p)
    for item in core['objects']:
        if sha(Path(item['object']))!=item['sha256']:raise ValueError('Changed core object')
    def run(cmd,name):
        r=subprocess.run([str(x) for x in cmd],capture_output=True,text=True);(out/name).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr[-4000:] or str(r.returncode))
        return r.stdout
    cc=Path(core['compiler'][0]);oracle=out/'oracle.exe';run([*a.host_cc,'-O2',pc/'tests/x87_trig_oracle.c','-o',oracle],'native-compiler.log')
    host_source=out/'host-negative.c';host_source.write_text('''#include "x87_trig_candidate.h"
#include <string.h>
int main(void){struct fnv_x87_stack s={0},before;s.control=0x37f;s.occupied=1;
for(unsigned op=0;op<4;op++){memcpy(&before,&s,sizeof(s));if(fnv_x87_trig_candidate(&s,op)!=FNV_X87_STACK_UNSUPPORTED || memcmp(&s,&before,sizeof(s)))return 1;}
if(fnv_x87_trig_candidate(0,0)!=FNV_X87_STACK_INVALID || fnv_x87_trig_candidate(&s,4)!=FNV_X87_STACK_INVALID)return 2;return 0;}
''')
    host_negative=out/'host-negative.exe';run([*a.host_cc,'-O2','-I'+str(pc/'native'),host_source,pc/'native/x87_trig_candidate.c','-o',host_negative],'host-negative-compiler.log');run([host_negative],'host-negative.log')
    frame();install_profile(profile);decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True;disassembler=Disassembler();lifter=Lifter();emitted={}
    for op,code in enumerate(('d9fe','d9ff','d9f2','d9fb')):
        ins=disassembler._decode_instruction(next(decoder.disasm(bytes.fromhex(code),0x400000)))
        emitted[op]='\n'.join(lifter.lift_instruction(ins)).replace('fnv_x87_state','s')
    entry=out/'entry.c';entry.write_text('''#include "x87_trig_candidate.h"
#include <stdint.h>
static int error_value;int *__errno(void){return &error_value;}
void *memset(void *p,int c,__SIZE_TYPE__ n){volatile unsigned char *b=p;for(__SIZE_TYPE__ i=0;i<n;i++)b[i]=(unsigned char)c;return p;}
__attribute__((section(".text.entry"))) void probe(const unsigned char *in,unsigned char *out){
struct fnv_x87_stack s;fnv_x87_stack_init(&s);s.control=in[10]|(uint16_t)in[11]<<8;
for(unsigned i=0;i<8;i++)s.registers[i]=(struct fnv_x87_extended){UINT64_C(0x8000000000000000),0x3fff};
s.top=in[13]==1?0:6;s.occupied=in[13]==1?0xff:in[13]==2?0xe0:in[13]==3?0x80:0xc0;
s.registers[s.top]=fnv_x87_unpack(in);s.status=(uint16_t)(((unsigned)s.top<<11)|0x600|(in[13]==4?0x80:0));
struct fnv_x87_stack before=s;struct fnv_x87_environment fnv_x87_environment_state={0};unsigned result=FNV_X87_STACK_OK;
#define RECOMP_UNIMPL(message,va) do {result=FNV_X87_STACK_UNSUPPORTED;goto done;} while(0)
switch(in[12]){'''+''.join('case '+str(op)+':'+code+'break;' for op,code in emitted.items())+'''default:result=fnv_x87_trig_candidate(&s,in[12]);break;}
done:;
unsigned count=in[13]==1?3:s.top==5?3:2;
for(unsigned i=0;i<3;i++){if(i<count)fnv_x87_pack(out+10*i,s.registers[(s.top+i)&7]);else for(unsigned j=0;j<10;j++)out[10*i+j]=0;}
out[30]=(unsigned char)s.status;out[31]=(unsigned char)(s.status>>8);out[32]=s.occupied;out[33]=s.top;out[34]=(unsigned char)result;
unsigned same=1,preserved=1;const unsigned char *a=(const unsigned char *)&s,*b=(const unsigned char *)&before;
for(unsigned i=0;i<sizeof(s);i++)if(a[i]!=b[i])same=0;
for(unsigned i=0;i<8;i++)if(i!=before.top && !(s.top!=before.top && i==s.top) && (s.registers[i].significand!=before.registers[i].significand || s.registers[i].sign_exponent!=before.registers[i].sign_exponent))preserved=0;
out[35]=(unsigned char)same;out[36]=(unsigned char)preserved;out[37]=(unsigned char)(s.control==before.control);
if(result==FNV_X87_STACK_OK && (fnv_x87_environment_state.instruction_pointer!=0x400000 || fnv_x87_environment_state.code_selector!='''+str(profile['code_selector'])+''' || fnv_x87_environment_state.opcode!=(in[12]==0?0x1fe:in[12]==1?0x1ff:in[12]==2?0x1f2:0x1fb)))out[34]=254;
}
''')
    rsp=out/'objects.rsp';rsp.write_text('\n'.join('"'+Path(i['object']).as_posix()+'"' for i in core['objects'])+'\n')
    layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    elf=out/'probe.elf';binary=out/'probe.bin';libs=[]
    for name in ('libm.a','libgcc.a'):
        p=Path(run([cc,'-print-file-name='+name],name+'-path.log').strip()).resolve();libs.append(dict(path=str(p),sha256=sha(p)))
    run([cc,'-O2','-fno-builtin','-ffreestanding','-fno-stack-protector','-nostdlib','-I'+str(pc/'native'),entry,'@'+str(rsp),'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-lm','-lgcc','-o',elf],'arm-compiler.log')
    run([cc.with_name('aarch64-none-elf-objcopy.exe'),'-O','binary',elf,binary],'objcopy.log')
    arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x300000);arm.mem_write(0x100000,binary.read_bytes());arm.mem_map(0x800000,0x10000)
    def raw(sig,e):return struct.pack('<QH',sig,e)
    values=[bytes(10),raw(0,0x8000),raw(1<<63,0x7fff),raw(1<<63,0xffff),raw(0xc000000000001234,0x7fff),raw(0xc000000000005678,0xffff),raw(1<<63,0x403e),raw(1<<63,0xc03e),raw((1<<64)-1,0x7ffe)]
    values.extend(raw(sig,e|sign) for e in (0x3fff-1000,0x3ffe,0x3fff,0x403d) for sig in (1<<63,(1<<64)-1) for sign in (0,0x8000))
    values.extend([raw(0xc90fdaa22168c235,0x4000),raw(0xc90fdaa22168c235,0x3fff)])
    rng=random.Random(0x51c05)
    values.extend(raw(rng.randrange(1<<63,1<<64),(rng.randrange(-20,63)+0x3fff)|(0x8000 if rng.randrange(2) else 0)) for unused in range(96))
    with localcontext() as ctx:
        ctx.prec=310;pi=16*atan_series(Decimal(1)/5)-4*atan_series(Decimal(1)/239)
        # Exercise neighborhoods of both quadrant zeros and quadrant-choice
        # boundaries, including large arguments close to the C2 limit.
        for n in (1,2,3,15,(1<<20)+1,(1<<40)+1,(1<<60)+1,(1<<62)-1):
            for offset in (Decimal(0),Decimal('0.5')):
                payload=round_normal((Decimal(n)+offset)*pi/2,0)[0];sig,e=struct.unpack('<QH',payload)
                for step in (-1,0,1):
                    q=sig+step;ex=e
                    if q<1<<63:q=(q<<1)|1;ex-=1
                    if q>=1<<64:q>>=1;ex+=1
                    values.extend((raw(q,ex),raw(q,ex|0x8000)))
    records=[]
    for pc_bits in (0,2,3):
        for rc in range(4):
            for op in range(4):
                records.extend(x+struct.pack('<HBBBB',0x7f|(pc_bits<<8)|(rc<<10),op,0,0,0) for x in values)
                if op<2:records.extend(x+struct.pack('<HBBBB',0x7f|(pc_bits<<8)|(rc<<10),op,1,0,0) for x in values[:9] if 0x403e<=int.from_bytes(x[8:10],'little')&0x7fff<0x7fff)
    data=b''.join(records);(out/'inputs.bin').write_bytes(data);native=subprocess.run([str(oracle)],input=data,capture_output=True)
    if native.returncode or len(native.stdout)!=40*len(records):raise RuntimeError('Native oracle failed')
    (out/'native.bin').write_bytes(native.stdout)
    def execute(record,index):
        for i in range(29):arm.reg_write(getattr(ar,'UC_ARM64_REG_X'+str(i)),0)
        arm.mem_write(0x800100,record);arm.mem_write(0x800200,b'\xa5'*48);fpcr=(index%4)<<22;fpsr=0x11
        for reg,v in ((ar.UC_ARM64_REG_X0,0x800100),(ar.UC_ARM64_REG_X1,0x800200),(ar.UC_ARM64_REG_SP,0x80f000),(ar.UC_ARM64_REG_LR,0x3ff000),(ar.UC_ARM64_REG_FPCR,fpcr),(ar.UC_ARM64_REG_FPSR,fpsr)):arm.reg_write(reg,v)
        arm.emu_start(0x100000,0x3ff000,count=2000000)
        if arm.reg_read(ar.UC_ARM64_REG_PC)!=0x3ff000:raise RuntimeError('Execution budget '+str(index))
        if arm.reg_read(ar.UC_ARM64_REG_FPCR)!=fpcr or arm.reg_read(ar.UC_ARM64_REG_FPSR)!=fpsr:raise RuntimeError('Host FP state changed '+str(index))
        z=bytes(arm.mem_read(0x800200,48))
        if z[38:]!=b'\xa5'*10 or z[36:38]!=b'\1\1':raise RuntimeError('Register/control/output guard '+str(index))
        return z
    def key(x):
        sig,se=struct.unpack('<QH',x);e=se&0x7fff
        if e==0x7fff:return None
        k=((e-1)<<63)+sig if e else sig;return -k if se&0x8000 else k
    failures=[];math_failures=[];native_differences=Counter();math_c1=0;math_cases=0;max_ulp=0;results=[]
    for index,record in enumerate(records):
        z=execute(record,index);results.append(z);ref=native.stdout[40*index:40*(index+1)];op=record[12];e=int.from_bytes(record[8:10],'little')&0x7fff;cw=int.from_bytes(record[10:12],'little');rc=(cw>>10)&3
        sw=int.from_bytes(z[30:32],'little');nsw=int.from_bytes(ref[30:32],'little')
        if z[34] or z[32:34]!=ref[32:34] or (sw^nsw)&0x3c3f:failures.append(dict(case=index,reason='status/stack',actual=z[:38].hex(),native=ref.hex()))
        strict=not e or e==0x7fff or e>=0x403e
        if strict and z[:30]!=ref[:30]:failures.append(dict(case=index,reason='special payload',actual=z[:30].hex(),native=ref[:30].hex()))
        if z[:30]!=ref[:30]:native_differences[op]+=1
        if not strict and e>=0x3fff-20:
            maths=reference(record[:10]);wanted=[maths[op if op<3 else 1][rc]]
            if op==2:wanted=[(raw(1<<63,0x3fff),0),maths[2][rc]]
            if op==3:wanted=[maths[1][rc],maths[0][rc]]
            for slot,(payload,c1) in enumerate(wanted):
                akey,bkey=key(z[10*slot:10*(slot+1)]),key(payload);distance=abs(akey-bkey) if akey is not None and bkey is not None else None
                if distance is None or distance>2:math_failures.append(dict(case=index,slot=slot,ulp=distance,actual=z[10*slot:10*(slot+1)].hex(),expected=payload.hex()))
                if distance is not None:max_ulp=max(max_ulp,distance)
            c1=wanted[1 if op==2 else 0][1];math_c1+=bool(sw&0x200)!=bool(c1);math_cases+=1
    (out/'actual.bin').write_bytes(b''.join(results))
    negative=[];one=raw(1<<63,0x3fff)
    for op in range(4):
        for x,cw,mode in ((one,0x37e,0),(one,0x17f,0),(one,0x37f,3),(one,0x37f,4),(raw(1,0),0x37f,0),(raw(1,0x3fff),0x37f,0),(raw(0x8000000000000001,0x7fff),0x37f,0),(raw(1<<63,0x3fff-1001),0x37f,0)):
            negative.append(x+struct.pack('<HBBBB',cw,op,mode,0,0))
        if op>=2:
            negative.extend(one+struct.pack('<HBBBB',0x37f,op,mode,0,0) for mode in (1,2))
            negative.append(raw(1<<63,0x403e)+struct.pack('<HBBBB',0x37f,op,1,0,0))
    negative.append(one+struct.pack('<HBBBB',0x37f,4,0,0,0));negative_bad=[]
    for index,record in enumerate(negative):
        z=execute(record,index)
        if not z[34] or not z[35]:negative_bad.append(dict(case=index,actual=z.hex()))
    # Real fault injection: compile isolated copies against zero/corrupt pi
    # tables, without altering the verified core or its generated header.
    table_path=next(Path(p) for p in core['source_sha256'] if Path(p).name=='x87_two_over_pi.h')
    words=[int(x,16) for x in re.findall(r'0x[0-9a-f]+',table_path.read_text())]
    if len(words)!=4:raise ValueError('Unexpected generated table')
    injection=[];saved_arm=arm
    for kind in ('zero','wrong-phase'):
        folder=out/('inject-'+kind);folder.mkdir();coefficients=[0]*4 if kind=='zero' else [*words[:3],words[3]^(1<<63)]
        header=folder/'x87_two_over_pi.h';header.write_text('static const uint64_t x87_two_over_pi[4]={'+','.join('UINT64_C('+hex(x)+')' for x in coefficients)+'};\n')
        obj=folder/'trig.o';injected_elf=folder/'probe.elf';injected_binary=folder/'probe.bin'
        run([cc,'-I'+str(folder),*core['flags'],'-c',pc/'native/x87_trig_candidate.c','-o',obj],kind+'-compiler.log')
        selected=[Path(i['object']) for i in core['objects'] if Path(i['source']).name!='x87_trig_candidate.c']+[obj]
        response=folder/'objects.rsp';response.write_text('\n'.join('"'+p.as_posix()+'"' for p in selected)+'\n')
        run([cc,'-O2','-fno-builtin','-ffreestanding','-fno-stack-protector','-nostdlib','-I'+str(pc/'native'),entry,'@'+str(response),'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-lm','-lgcc','-o',injected_elf],kind+'-link.log')
        run([cc.with_name('aarch64-none-elf-objcopy.exe'),'-O','binary',injected_elf,injected_binary],kind+'-objcopy.log')
        arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x300000);arm.mem_write(0x100000,injected_binary.read_bytes());arm.mem_map(0x800000,0x10000)
        z=execute(one+struct.pack('<HBBBB',0x37f,0,0,0,0),0)
        if kind=='zero':
            if not z[34] or not z[35]:raise RuntimeError('Zero-table uncertainty was not rejected atomically')
        else:
            wanted=reference(one)[0][0][0]
            if z[34] or key(z[:10]) is None or abs(key(z[:10])-key(wanted))<=2:raise RuntimeError('Wrong phase table did not fail numeric acceptance')
        injection.append(dict(kind=kind,rejected=True,header_sha256=sha(header),object_sha256=sha(obj),elf_sha256=sha(injected_elf)))
    arm=saved_arm
    for p,h in frozen.items():
        if sha(Path(p))!=h:raise ValueError('Changed frozen source '+p)
    for item in libs:
        if sha(Path(item['path']))!=item['sha256']:raise ValueError('Changed math library')
    status='FAIL' if failures or math_failures or negative_bad else 'PASS_SELECTED_TRUE_PI_MODEL'
    result=dict(status=status,native_cases=len(records),true_pi_reference_cases=math_cases,max_math_ulp=max_ulp,native_payload_differences=dict(native_differences),math_c1_differences=math_c1,
        state_failures=failures,math_failures=math_failures,negative_cases=len(negative),negative_failures=negative_bad,host_fail_closed_cases=6,fault_injections=injection,
        source_sha256=frozen,libraries=libs,sdk_sha256=sdk_identity(a.sdk.resolve()),target_elf_sha256=sha(elf),
        generated_sha256={n:sha(out/n) for n in ('inputs.bin','native.bin','actual.bin','entry.c','layout.ld')},full_game_generation_enabled=False,switch_hardware_tested=False,
        limits=['True-pi kernel policy, not native x87 bit identity','Math value tolerance up to two ext80 ULP on selected samples','C1 reflects kernel rounding','No complete-domain correctness or performance claim'])
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({k:result[k] for k in ('status','native_cases','true_pi_reference_cases','max_math_ulp','native_payload_differences','math_c1_differences','negative_cases')},indent=2));print('failures',len(failures),len(math_failures),len(negative_bad))
    return 1 if status=='FAIL' else 0


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('core','profile','sdk','python-deps','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--host-cc',nargs='+',required=True)
    raise SystemExit(test(parser.parse_args()))

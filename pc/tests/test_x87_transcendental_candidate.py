"""Exercise an isolated ARM64 candidate; never enables production translation.

Native x87 is the observable-value/status oracle. C1 differences are reported
as unresolved findings, not hidden by the numeric tolerance. No game inputs.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    out=a.output.resolve();out.mkdir(parents=True);pc=Path(__file__).resolve().parents[1]
    proof=json.loads(a.arm_build.read_text())
    if proof['status']!='COMPILED':raise ValueError('Complete ARM core required')
    frozen={str(Path(p).resolve()):h for p,h in proof['source_sha256'].items()}
    sources=[Path(__file__).resolve(),pc/'tests/x87_transcendental_oracle.c',
             pc/'native/x87_transcendental_candidate.c',pc/'native/x87_transcendental_candidate.h']
    frozen.update({str(p):sha(p) for p in [*sources,a.cc.resolve(),Path(a.host_cc[0]).resolve(),a.arm_build.resolve(),
        *[pc/'tools'/n for n in ('build_cpu_runtime.py','aot_x87_extended.py','aot_frame_restore.py','run_aot_analysis.py')]]})
    if a.x87_profile:frozen[str(a.x87_profile.resolve())]=sha(a.x87_profile.resolve())
    for p,h in frozen.items():
        if sha(Path(p))!=h:raise ValueError('Changed dependency '+p)
    objects=[]
    for item in proof['objects']:
        p=Path(item['object'])
        if sha(p)!=item['sha256']:raise ValueError('Changed core object')
        objects.append(p)
    candidate_in_core=any(Path(item['source']).name=='x87_transcendental_candidate.c' for item in proof['objects'])
    def run(cmd,log):
        r=subprocess.run([str(x) for x in cmd],capture_output=True,text=True);(out/log).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr[-5000:] or str(r.returncode))
        return r.stdout
    native=out/'oracle.exe';run([*a.host_cc,'-O2',sources[1],'-o',native],'native-compiler.log')
    entry=out/'entry.c';entry.write_text('''#include "x87_transcendental_candidate.h"
#include <stdint.h>
static int error_value;int *__errno(void){return &error_value;}
void *memset(void *p,int c,__SIZE_TYPE__ n){volatile unsigned char *b=p;for(__SIZE_TYPE__ i=0;i<n;i++)b[i]=(unsigned char)c;return p;}
__attribute__((section(".text.entry"))) void probe(const unsigned char *in,unsigned char *out){
struct fnv_x87_stack s;fnv_x87_stack_init(&s);s.control=in[20]|(uint16_t)in[21]<<8;
/* Loading is separate from the operation, including invalid-input tests. */
s.registers[6]=fnv_x87_unpack(in);s.registers[7]=fnv_x87_unpack(in+10);s.top=6;s.occupied=in[23]?in[23]:0xc0;s.status=0x3000;
if(in[23]==0xff){s.occupied=0xc0;s.status|=0x80;}
struct fnv_x87_stack before=s;
unsigned result=fnv_x87_transcendental_candidate(&s,in[22]);
fnv_x87_pack(out,s.registers[s.top]);if(!in[22])fnv_x87_pack(out+10,s.registers[(s.top+1)&7]);else for(unsigned i=10;i<20;i++)out[i]=0;
out[20]=(unsigned char)s.status;out[21]=(unsigned char)(s.status>>8);out[22]=s.occupied;out[23]=s.top;out[24]=(unsigned char)result;
const unsigned char *a=(const unsigned char *)&s,*b=(const unsigned char *)&before;unsigned same=1;
for(unsigned i=0;i<sizeof(s);i++)if(a[i]!=b[i])same=0;out[25]=(unsigned char)same;
out[26]=(unsigned char)s.control;out[27]=(unsigned char)(s.control>>8);
unsigned preserved=1,d=in[22]?7:6;
for(unsigned i=0;i<8;i++)if(i!=d && (s.registers[i].significand!=before.registers[i].significand || s.registers[i].sign_exponent!=before.registers[i].sign_exponent))preserved=0;
out[28]=(unsigned char)preserved;
}
''')
    layout=out/'layout.ld';layout.write_text('ENTRY(probe)\nSECTIONS { . = 0x100000; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } .data : { *(.data*) } .bss : { *(.bss*) *(COMMON) } }\n')
    rsp=out/'objects.rsp';rsp.write_text('\n'.join('"'+p.as_posix()+'"' for p in objects)+'\n')
    libraries=[]
    for name in ('libm.a','libgcc.a'):
        p=Path(run([a.cc,'-print-file-name='+name],name+'-path.log').strip()).resolve()
        if not p.is_file():raise ValueError('Missing library')
        libraries.append(dict(path=str(p),sha256=sha(p)))
    host_source=out/'host-negative.c';host_source.write_text('''#include "x87_transcendental_candidate.h"
#include <string.h>
int main(void){struct fnv_x87_stack s={0},before;s.control=0x37f;s.occupied=1;
for(unsigned i=0;i<3;i++){before=s;if(fnv_x87_transcendental_candidate(&s,i)!=FNV_X87_STACK_UNSUPPORTED || memcmp(&s,&before,sizeof(s)))return 1;}
if(fnv_x87_transcendental_candidate(0,0)!=FNV_X87_STACK_INVALID)return 2;return 0;}
''')
    host_negative=out/'host-negative.exe'
    run([*a.host_cc,'-O2','-I'+str(pc/'native'),host_source,sources[2],'-o',host_negative],'host-negative-compiler.log')
    run([host_negative],'host-negative.log')
    elf=out/'probe.elf';binary=out/'probe.bin'
    flags=['-std=c11','-O2','-frounding-math','-ffp-contract=off','-fno-builtin','-ffreestanding','-fno-stack-protector','-ffunction-sections','-fdata-sections','-I'+str(pc/'native')]
    sys.path[:0]=[str(a.python_deps.resolve()),str(pc/'tools')]
    from build_cpu_runtime import SWITCH_FLAGS
    run([a.cc,*SWITCH_FLAGS,'-frounding-math','-fno-builtin','-I'+str(pc/'native'),'-c',sources[2],'-o',out/'candidate-switch.o'],'switch-compiler.log')
    # The real SDK decoder/lifter is exercised with an isolated opt-in hook.
    # The normal AOT emitter must still reject these three instructions.
    sys.path.insert(0,str(a.sdk.resolve()))
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    from tools.recomp.disasm import Disassembler
    from tools.recomp.lifter import Lifter
    from aot_frame_restore import install as frame
    from aot_x87_extended import install as extended,emit
    from run_aot_analysis import sdk_identity
    sdk_hash=sdk_identity(a.sdk.resolve());frame();extended()
    original=Lifter.lift_instruction
    operations={'f2xm1':0,'fpatan':1,'fyl2x':2}
    def isolated_hook(self,instruction):
        if instruction.mnemonic not in operations:return original(self,instruction)
        return ['result=fnv_x87_transcendental_candidate(&s,'+str(operations[instruction.mnemonic])+');']
    selected_profile=None
    if a.x87_profile:
        from aot_x87_codegen import load_profile,install_profile
        selected_profile=load_profile(a.x87_profile)
        if not candidate_in_core or selected_profile.get('transcendental_model')!=proof.get('transcendental_model'):raise ValueError('Profile/backend mismatch')
        install_profile(selected_profile)
    else:Lifter.lift_instruction=isolated_hook
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True;disassembler=Disassembler();lifter=Lifter();lifted={}
    for op,encoded in enumerate(('d9f0','d9f3','d9f1')):
        ins=disassembler._decode_instruction(next(decoder.disasm(bytes.fromhex(encoded),0x400000)))
        if not selected_profile and emit(ins) is not None:raise ValueError('Strict emitter was unexpectedly enabled')
        lifted[op]=lifter.lift_instruction(ins)
    emitted={op:'\n'.join(code).replace('fnv_x87_state','s') for op,code in lifted.items()}
    text=entry.read_text();text=text.replace('unsigned result=fnv_x87_transcendental_candidate(&s,in[22]);',
        'struct fnv_x87_environment fnv_x87_environment_state={0};\n#define RECOMP_UNIMPL(message,va) do {result=FNV_X87_STACK_UNSUPPORTED;goto candidate_done;} while(0)\n'
        'unsigned result=FNV_X87_STACK_OK;switch(in[22]){'+''.join('case '+str(op)+':'+code+'break;' for op,code in emitted.items())+'default:result=fnv_x87_transcendental_candidate(&s,in[22]);break;}\ncandidate_done:;')
    if selected_profile:
        check='if(result==FNV_X87_STACK_OK && (fnv_x87_environment_state.instruction_pointer!=0x400000 || fnv_x87_environment_state.code_selector!='+str(selected_profile['code_selector'])
        if selected_profile['opcode_compatibility']:check+=' || fnv_x87_environment_state.opcode!=(in[22]==0?0x1f0:in[22]==1?0x1f3:0x1f1)'
        check+='))out[24]=254;'
        text=text.replace('out[28]=(unsigned char)preserved;','out[28]=(unsigned char)preserved;'+check)
    entry.write_text(text)
    run([a.cc,*flags,'-nostdlib',entry,*([] if candidate_in_core else [sources[2]]),'@'+str(rsp),'-Wl,--gc-sections','-Wl,-T,'+str(layout),'-lm','-lgcc','-o',elf],'arm-lifted-compiler.log')
    run([a.cc.with_name('aarch64-none-elf-objcopy.exe'),'-O','binary',elf,binary],'lifted-objcopy.log')
    from unicorn import Uc,UcError,UC_ARCH_ARM64,UC_MODE_ARM
    import unicorn.arm64_const as ar
    arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x200000);arm.mem_write(0x100000,binary.read_bytes());arm.mem_map(0x800000,0x10000)
    def raw(sig,e):return struct.pack('<QH',sig,e)
    def dyadic(n,scale=0):
        if not n:return bytes(10)
        bits=abs(n).bit_length();return raw(abs(n)<<(64-bits),(0x8000 if n<0 else 0)|((bits-1-scale)+0x3fff))
    basic=[bytes(10),raw(0,0x8000),dyadic(1),dyadic(-1),dyadic(1,1),dyadic(-1,1),dyadic(2),dyadic(-2),
           raw(1<<63,0x7fff),raw(1<<63,0xffff),raw(0xc000000000001234,0x7fff),raw(0xc000000000005678,0xffff)]
    pairs=[(0,x,dyadic(3),True) for x in basic if (struct.unpack('<H',x[8:])[0]&0x7fff)<0x4000 or x[7]&0x40]
    pairs.extend((op,x,y,True) for op in (1,2) for x in basic for y in basic)
    rng=random.Random(0x87a7a)
    def operand(positive=False):return raw(rng.randrange(1<<63,1<<64),(0 if positive or rng.randrange(2)==0 else 0x8000)|(rng.randrange(-100,101)+0x3fff))
    for unused in range(192):
        pairs.extend([(0,dyadic(rng.randrange(-(1<<52),1<<52),52),dyadic(3),False),
                      (1,operand(),operand(),False),(2,operand(True),operand(),False)])
    reference_pair_count=len(pairs)
    if a.extreme_sweep:
        for e in (-1000,-999,-500,500,999,1000):
            for sig in (1<<63,(1<<63)+1,(1<<64)-1):
                for sign in (0,0x8000):
                    x=raw(sig,sign|(0x3fff+e));opposite=raw(sig,0x3fff-e)
                    if e<0:pairs.append((0,x,dyadic(3),False))
                    pairs.extend([(1,x,opposite,False),(2,raw(sig,0x3fff+e),raw(sig,sign|(0x3fff+e)),False)])
    records=[];special=[];math_cases=[];extreme_cases=[]
    for pc_bits in (0,2,3):
        for rc in range(4):
            for pair_index,(op,x,y,strict) in enumerate(pairs):
                index=len(records)
                records.append(x+y+struct.pack('<HBB',0x7f|(pc_bits<<8)|(rc<<10),op,0));special.append(strict)
                if not strict and pair_index<reference_pair_count:math_cases.append(index)
                if pair_index>=reference_pair_count:extreme_cases.append(index)
    data=b''.join(records);(out/'inputs.bin').write_bytes(data)
    native_result=subprocess.run([str(native)],input=data,capture_output=True);(out/'native.log').write_bytes(native_result.stderr)
    if native_result.returncode or len(native_result.stdout)!=len(records)*24:raise RuntimeError('Native oracle failed')
    expected=native_result.stdout;(out/'expected.bin').write_bytes(expected)
    def execute(record,case):
        for i in range(29):arm.reg_write(getattr(ar,'UC_ARM64_REG_X'+str(i)),0)
        arm.mem_write(0x800100,record);arm.mem_write(0x800200,b'\xa5'*32)
        host_fpcr=(case%4)<<22;host_fpsr=0x11
        for reg,value in ((ar.UC_ARM64_REG_X0,0x800100),(ar.UC_ARM64_REG_X1,0x800200),(ar.UC_ARM64_REG_SP,0x80f000),(ar.UC_ARM64_REG_LR,0x2ff000),(ar.UC_ARM64_REG_FPCR,host_fpcr),(ar.UC_ARM64_REG_FPSR,host_fpsr)):arm.reg_write(reg,value)
        arm.emu_start(0x100000,0x2ff000,count=2000000)
        if arm.reg_read(ar.UC_ARM64_REG_PC)!=0x2ff000:raise RuntimeError('Instruction budget '+str(case))
        if arm.reg_read(ar.UC_ARM64_REG_FPCR)!=host_fpcr or arm.reg_read(ar.UC_ARM64_REG_FPSR)!=host_fpsr:raise RuntimeError('Host FP state changed '+str(case))
        value=bytes(arm.mem_read(0x800200,32))
        if value[29:]!=b'\xa5'*3:raise RuntimeError('Output bounds '+str(case))
        if value[26:28]!=record[20:22] or value[28]!=1:raise RuntimeError('Control/unselected registers changed '+str(case))
        return value
    def key(x):
        sig,se=struct.unpack('<QH',x);e=se&0x7fff
        if e==0x7fff:return None
        k=((e-1)<<63)+sig if e else sig
        return -k if se&0x8000 else k
    failures=[];c1=[];numeric=Counter();maximum=Counter();flags_bad=[];actual_records=[]
    for index,record in enumerate(records):
        result=execute(record,index);actual_records.append(result);ref=expected[24*index:24*(index+1)];op=record[22]
        actual_key,expected_key=key(result[:10]),key(ref[:10]);distance=abs(actual_key-expected_key) if actual_key is not None and expected_key is not None else None
        if result[24] or (result[22:24]!=ref[22:24]) or (not op and result[10:20]!=ref[10:20]):failures.append(dict(case=index,reason='status/stack/canary',actual=result.hex(),native=ref.hex()))
        if (special[index] and result[:10]!=ref[:10]) or (not special[index] and (distance is None or distance>2)):failures.append(dict(case=index,reason='numeric',ulp=distance,actual=result[:10].hex(),native=ref[:10].hex()))
        if result[:10]==ref[:10]:numeric[op]+=1
        if distance is not None:maximum[op]=max(maximum[op],distance)
        sw,nsw=struct.unpack('<H',result[20:22])[0],struct.unpack('<H',ref[20:22])[0]
        if (sw^nsw)&0x383f:flags_bad.append(dict(case=index,actual=sw,native=nsw))
        if (sw^nsw)&0x200:c1.append(dict(case=index,actual=sw,native=nsw,ulp=distance))
    bad=[];one=dyadic(1)
    invalid=[(raw(1,0),one,0x37f,op,0) for op in range(3)]
    invalid.extend((raw(0x8000000000000001,0x7fff),one,0x37f,op,0) for op in range(3))
    invalid.extend((one,one,cw,op,0) for cw in (0x37e,0x17f) for op in range(3))
    invalid.extend((one,one,0x37f,op,0x80) for op in range(3))
    invalid.extend((one,one,0x37f,op,0x40) for op in (1,2))
    invalid.extend([(dyadic(2),one,0x37f,0,0),(raw(1<<63,0x3fff-1001),one,0x37f,0,0),(one,one,0x37f,3,0)])
    invalid.extend((one,one,0x37f,op,0xff) for op in range(3))
    for op in (1,2):
        for e in (1,0x3fff-1001,0x3fff+1001,0x7ffe):
            invalid.extend([(raw(1<<63,e),dyadic(2),0x37f,op,0),(dyadic(2),raw(1<<63,e),0x37f,op,0)])
        invalid.extend((dyadic(2),raw(sig,e),0x37f,op,0) for sig,e in ((1,0),(0x8000000000000001,0x7fff),(1,0x3fff)))
    for index,(x,y,cw,op,occupied) in enumerate(invalid):
        result=execute(x+y+struct.pack('<HBB',cw,op,occupied),index)
        if not result[24] or not result[25]:bad.append(dict(case=index,actual=result.hex()))
    for p,h in frozen.items():
        if sha(Path(p))!=h:raise ValueError('Changed dependency '+p)
    for item in libraries:
        if sha(Path(item['path']))!=item['sha256']:raise ValueError('Changed library')
    if sdk_identity(a.sdk.resolve())!=sdk_hash:raise ValueError('Changed SDK')
    (out/'actual.bin').write_bytes(b''.join(actual_records))
    status='CANDIDATE_FAILED' if failures or flags_bad or bad else 'CANDIDATE_WITH_C1_DIFFERENCES' if c1 else 'CANDIDATE_MATCHED_SELECTED_CASES'
    result=dict(status=status,cases=len(records),special_cases=sum(special),native_payloads_exact=dict(numeric),max_ulp=dict(maximum),
                failures=failures,exception_or_stack_flag_failures=flags_bad,c1_differences=c1,negative_cases=len(invalid),negative_failures=bad,
                source_sha256=frozen,libraries=libraries,elf_sha256=sha(elf),target_flags=flags,sdk_sha256=sdk_hash,
                lifted_opcodes=3,explicit_generation_profile=selected_profile,host_fail_closed_cases=4,backend_from_core_proof=candidate_in_core,
                ordinary_case_indices=[i for i,strict in enumerate(special) if not strict],
                math_reference_case_indices=math_cases,extreme_case_indices=extreme_cases,
                generated_sha256={name:sha(out/name) for name in ('inputs.bin','expected.bin','actual.bin','entry.c','layout.ld')},
                aot_profile_enabled=selected_profile is not None,full_game_generation_enabled=False,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('status','cases','special_cases','native_payloads_exact','max_ulp','negative_cases')},indent=2));print('failures',len(failures),'flags',len(flags_bad),'C1',len(c1),'negative',len(bad))
    # A generic test runner must not mistake unresolved C1 for a passing
    # translation. Keep the numeric/status findings, but fail acceptance.
    return 1 if failures or flags_bad or bad else 2 if c1 else 0


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('arm-build','cc','python-deps','sdk','output'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--host-cc',nargs='+',required=True)
    parser.add_argument('--x87-profile',type=Path)
    parser.add_argument('--extreme-sweep',action='store_true')
    raise SystemExit(test(parser.parse_args()))

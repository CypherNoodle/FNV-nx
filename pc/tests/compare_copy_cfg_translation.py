"""Compare supplied private copy translations against original x86 in Unicorn.

Compiles both C bodies to real ARM64. Inputs, generated probes and reports may
contain game code and must remain private. No full-game or hardware claim.
"""
from pathlib import Path
import argparse,hashlib
parser=argparse.ArgumentParser(description=__doc__)
for name in ('legacy-source','corrected-source','code','sdk','python-deps','cc','output'):
 parser.add_argument('--'+name,type=Path,required=True)
for name in ('entry','code-base','image-base','image-bytes'):
 parser.add_argument('--'+name,type=lambda s:int(s,0),required=True)
a=parser.parse_args()
import sys,subprocess,struct,random,json,re
root=Path(__file__).resolve().parents[2];sys.path[:0]=[str(a.python_deps.resolve()),str(root/'pc/tools')]
from run_aot_analysis import sdk_identity
sdk_hash=sdk_identity(a.sdk.resolve())
from unicorn import Uc,UC_ARCH_ARM64,UC_ARCH_X86,UC_MODE_ARM,UC_MODE_32,UcError
import unicorn.arm64_const as ar
import unicorn.x86_const as xr
out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
frozen={str(p.resolve()):hashlib.sha256(p.read_bytes()).hexdigest() for p in (a.legacy_source,a.corrected_source,a.code,a.cc,Path(__file__))}
body0=a.legacy_source.read_text().replace(f'sub_{a.entry:08X}','copy_legacy')
body1=a.corrected_source.read_text().replace(f'sub_{a.entry:08X}','copy_fixed')
prefix='#include "recomp_types.h"\ntypedef struct {uint32_t r[8],df,diagnostic;} State;\nstatic State *ctx;\nptrdiff_t g_xbox_mem_offset;\n'
for i,r in enumerate(('eax','ecx','edx','ebx','esp','ebp','esi','edi')):
 if r!='ebp':prefix+=f'#undef {r}\n#define {r} (ctx->r[{i}])\n'
prefix+='#define g_ebp (ctx->r[5])\n#define g_seh_ebp (ctx->r[5])\n#define g_df (ctx->df)\n#undef RECOMP_ITAIL\n#define RECOMP_ITAIL(a) do {ctx->diagnostic=(uint32_t)(a);return;} while(0)\nvoid recomp_unimpl(const char*s,uint32_t a){(void)s;ctx->diagnostic=a;}\nvoid *memcpy(void*d,const void*s,size_t n){unsigned char*a=d;const unsigned char*b=s;for(size_t i=0;i<n;i++)a[i]=b[i];return d;}\n'
# Any spurious x87 WAIT decoded from table data must fail the legacy control.
prefix+='static int fnv_x87_state;\n#define FNV_X87_STACK_OK 0\nstatic int fnv_x87_stack_wait(void *p){(void)p;ctx->diagnostic=0xF0000001;return 1;}\n'
prefix+=''.join('void '+name+'(void){ctx->diagnostic=0x'+name[4:]+';}\n' for name in sorted(set(re.findall(r'sub_[0-9A-Fa-f]{8}',body0+body1))))
source=prefix+body0+'\n'+body1+'\n__attribute__((section(".text.entry"))) void probe(State*s,uint32_t which){ctx=s;g_xbox_mem_offset=0;if(which)copy_fixed();else copy_legacy();}\n'
(out/'probe.c').write_text(source);(out/'layout.ld').write_text('ENTRY(probe)\nSECTIONS{. = 0x100000;.text :{*(.text.entry) *(.text*)}.rodata :{*(.rodata*)}.data :{*(.data*)}.bss :{*(.bss*) *(COMMON)}}')
cc=a.cc.resolve();args=[str(cc),'-std=gnu11','-O1','-mearly-ra=none','-fno-strict-aliasing','-fno-builtin','-ffreestanding','-fno-stack-protector','-nostdlib','-I'+str(a.sdk.resolve()/'templates/runtime'),'-Wl,-T,'+str(out/'layout.ld'),str(out/'probe.c'),'-o',str(out/'probe.elf')]
p=subprocess.run(args,capture_output=True,text=True);(out/'compiler.log').write_text(p.stdout+p.stderr);assert not p.returncode,p.stderr[-3000:]
subprocess.run([str(cc.with_name('aarch64-none-elf-objcopy.exe')),'-O','binary',str(out/'probe.elf'),str(out/'probe.bin')],check=True)
raw=a.code.read_bytes()
assert a.image_base<=a.code_base and a.code_base+len(raw)<=a.image_base+a.image_bytes<=0x2000000
assert not a.image_base%4096 and not a.image_bytes%4096
# Original global feature flag remains zero: exercise non-SSE CRT paths.
arm=Uc(UC_ARCH_ARM64,UC_MODE_ARM);arm.mem_map(0x100000,0x100000);arm.mem_write(0x100000,(out/'probe.bin').read_bytes())
x86=Uc(UC_ARCH_X86,UC_MODE_32)
for u in (arm,x86):u.mem_map(a.image_base,a.image_bytes);u.mem_write(a.code_base,raw);u.mem_map(0x2000000,0x10000);u.mem_map(0x2100000,0x10000);u.mem_map(0x3000000,0x10000)
regs=[getattr(xr,'UC_X86_REG_'+r) for r in ('EAX','ECX','EDX','EBX','ESP','EBP','ESI','EDI')];rng=random.Random(0xc044d0)
failures=[];negative_total=0;byte_shift=False;fixed=0;total=0
for size in list(range(0,66))+[95,127,128,191,255,256,257,511,512,1023,4096,8192]:
 for sa in range(4):
  for da in range(4):
   for overlap in (0,1,2):
    src=0x2000100+sa;dest=(0x2000800+da if not overlap else src+(1+da if overlap==1 else -1-da));stack=0x2108000
    data=rng.randbytes(16384);gpr=[0x11111111,0x22222222,0x33333333,0x44444444,stack,0x22000a20,0x0000000f,0x26000aa8]
    stackdata=struct.pack('<IIII',0x1ff000,dest,src,size)+bytes(64)
    x86.mem_write(0x2000000,data);x86.mem_write(stack,stackdata);x86.reg_write(xr.UC_X86_REG_EFLAGS,0x202)
    for r,v in zip(regs,gpr):x86.reg_write(r,v)
    x86.emu_start(a.entry,0x1ff000,count=200000)
    assert x86.reg_read(xr.UC_X86_REG_EIP)==0x1ff000
    expect=[x86.reg_read(r) for r in regs];expectedmem=bytes(x86.mem_read(0x2000000,16384))
    for which in (0,1):
     arm.mem_write(0x2000000,data);arm.mem_write(stack,stackdata);arm.mem_write(0x3000100,struct.pack('<10I',*gpr,0,0));arm.reg_write(ar.UC_ARM64_REG_X0,0x3000100);arm.reg_write(ar.UC_ARM64_REG_X1,which);arm.reg_write(ar.UC_ARM64_REG_SP,0x300f000);arm.reg_write(ar.UC_ARM64_REG_LR,0x1ff000)
     error=None
     try:arm.emu_start(0x100000,0x1ff000,count=1000000)
     except UcError as e:error=str(e)
     actual=struct.unpack('<10I',bytes(arm.mem_read(0x3000100,40)));mem=bytes(arm.mem_read(0x2000000,16384))
     ok=not error and arm.reg_read(ar.UC_ARM64_REG_PC)==0x1ff000 and not actual[9] and mem==expectedmem and all(actual[i]==expect[i] for i in range(8)) and actual[8]==0
     if which:
      assert ok,(size,sa,da,overlap,actual,expect,error)
      fixed+=1
     elif not ok:
      negative_total+=1
      byte_shift|=actual[6]==(gpr[6]>>8 | (gpr[7]&255)<<24) and actual[7]==(gpr[7]>>8 | (gpr[5]&255)<<24)
      if len(failures)<20:failures.append(dict(size=size,source_alignment=sa,dest_alignment=da,overlap=overlap,actual=list(actual),expected=expect,error=error))
    total+=1
assert failures
report=dict(status='PASS',differential_cases=fixed,negative_control_failure_count=negative_total,negative_control_failures=failures,legacy_byte_shift_reproduced=byte_shift,source_sha256=frozen,sdk_sha256=sdk_hash,all_gpr_compared=True,arm64_binary_sha256=hashlib.sha256((out/'probe.bin').read_bytes()).hexdigest(),coverage='sizes0..65 plus95,127,128,191,255,256,257,511,512,1023,4096,8192; all alignments; separate/forward/backward overlap; DFclear input; non-SSE profile',hardware_tested=False)
assert sdk_identity(a.sdk.resolve())==sdk_hash
assert all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==h for p,h in frozen.items())
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS',fixed,'cases; legacy negative failures',len(failures),failures[:1])

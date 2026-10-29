"""Differential synthetic guest registers/stack bytes for explicit leaf inlining."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import random
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from inline_generated_leaves import transform,function_span

PREFIX=r'''
#include <stdint.h>
#include <string.h>
static unsigned char memory[4096];
static uint32_t eax,ecx,edx,esp,ebx,esi,edi,g_ebp,g_seh_ebp;
#define MEM32(a) (*(volatile uint32_t*)(memory+(uint32_t)(a)))
#define PUSH32(s,v) do{(s)-=4;MEM32(s)=(v);}while(0)
#define POP32(s,v) do{(v)=MEM32(s);(s)+=4;}while(0)
#define RECOMP_ABI_CALL(va,fn) (fn)()
#define SET_LO8(a,b) ((a)=((a)&0xffffff00u)|((uint32_t)(b)&255u))
'''
LEAF=r'''
void sub_00001000(void){
    uint32_t ebp=g_ebp;
    PUSH32(esp,ebp);ebp=esp;g_ebp=ebp;g_seh_ebp=ebp;
    PUSH32(esp,ecx);MEM32(ebp-4)=ecx;eax=MEM32(ebp-4);
    eax=MEM32(eax+4);esp=ebp;POP32(esp,ebp);g_ebp=ebp;g_seh_ebp=ebp;
    esp+=4;return;
}
'''
BRANCH_LEAF=r'''
void sub_00001100(void){
    if(MEM32(ecx)==0){SET_LO8(eax,MEM32(ecx+4)==0);}
    else{SET_LO8(eax,0);}
    esp+=4;return;
}
'''
UNIT=PREFIX+r'''
void sub_00001000(void);
void sub_00001100(void);
void sub_00002000(void){
    /* RECOMP_ABI_CALL(0x1000u, sub_00001000) } { */
    PUSH32(esp,0x2005);RECOMP_ABI_CALL(0x1000u,sub_00001000);
    edx=eax;ecx=132;PUSH32(esp,0x200A);RECOMP_ABI_CALL(0x1000u,sub_00001000);
    ebx=edx^eax;
    ecx=128;PUSH32(esp,0x2020);RECOMP_ABI_CALL(0x1100u,sub_00001100);
}
void sub_00003000(void){eax=7;}
'''
SUFFIX=r'''
__declspec(dllexport) void run(const uint32_t *input,unsigned char *out){
    memcpy(memory,input,4096);eax=input[0];ecx=128;edx=input[2];esp=2048;
    ebx=input[3];esi=input[4];edi=input[5];g_ebp=input[6];g_seh_ebp=input[7];
    sub_00002000();memcpy(out,memory,4096);
    uint32_t regs[9]={eax,ecx,edx,esp,ebx,esi,edi,g_ebp,g_seh_ebp};memcpy(out+4096,regs,sizeof(regs));
}
'''

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    sources=[Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'tools/inline_generated_leaves.py',Path(a.cc).resolve()]
    frozen={str(p):sha(p) for p in sources}
    optimized,report=transform(UNIT,{'sub_00001000':LEAF,'sub_00001100':BRANCH_LEAF},{'sub_00002000'})
    assert report['call_sites']=={'sub_00001000':2,'sub_00001100':1}
    x,y=function_span(UNIT,'sub_00003000');assert UNIT[x:y] in optimized
    assert '/* RECOMP_ABI_CALL(0x1000u, sub_00001000) } { */' in optimized
    libraries=[]
    for name,text in [('original',UNIT),('inlined',optimized)]:
        src=a.output/(name+'.c');dll=a.output.resolve()/(name+'.dll');src.write_text(text+LEAF+BRANCH_LEAF+SUFFIX)
        subprocess.run([a.cc,'-O1','-std=c11','-fno-strict-aliasing','-Wall','-Wextra','-Werror','-shared',str(src),'-o',str(dll)],check=True)
        lib=C.CDLL(str(dll));lib.run.argtypes=[C.POINTER(C.c_uint32),C.POINTER(C.c_ubyte)];libraries.append(lib)
    rng=random.Random(54981);outputs=[(C.c_ubyte*4132)(),(C.c_ubyte*4132)()]
    for i in range(12000):
        data=(C.c_uint32*1024)(*[rng.getrandbits(32) for _ in range(1024)])
        if i%4<2:data[32]=0
        if i%4==0:data[33]=0
        for lib,out in zip(libraries,outputs):lib.run(data,out)
        assert bytes(outputs[0])==bytes(outputs[1]),i
    negatives=[(UNIT,LEAF.replace('eax=MEM32(eax+4);','eax=unknown(ecx);')),
               (UNIT,LEAF.replace('uint32_t ebp=g_ebp;','static uint32_t ebp;')),
               (UNIT.replace('0x1000u','0x1001u'),LEAF),
               (UNIT.replace('sub_00001000);','sub_00003000);'),LEAF),
               (UNIT+'\nvoid sub_00002000(void){}',LEAF),
               (UNIT.replace('edx=eax;','edx=__LINE__;'),LEAF),
               (UNIT,LEAF.replace('eax=MEM32(eax+4);','eax=__LINE__;'))]
    for unit,source in negatives:
        try:transform(unit,{'sub_00001000':source},{'sub_00002000'})
        except ValueError:pass
        else:raise AssertionError('Unsafe candidate accepted')
    assert all(sha(Path(n))==d for n,d in frozen.items())
    result=dict(status='PASS',synthetic_cases=12000,compared_state_bytes=4132,negative_cases=len(negatives),source_sha256=frozen,game_code_used=False,hardware_tested=False)
    (a.output/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))

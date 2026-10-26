"""Reuse a verified standalone math VM; reset all writable state per request.

Only diagnostic transport cost changes. The frozen ARM64 instructions and
numerical model are the same as x87_target_evaluator.Evaluator.
"""
from pathlib import Path
import struct
import threading
from x87_target_evaluator import Evaluator,sha


def writable_ranges(elf):
    if len(elf)<64 or elf[:6]!=b'\x7fELF\x02\x01' or struct.unpack_from('<H',elf,18)[0]!=183:
        raise ValueError('Require little-endian ELF64 ARM64 image')
    table=struct.unpack_from('<Q',elf,40)[0]
    size,count=struct.unpack_from('<HH',elf,58)
    if size!=64 or not 1<=count<=4096 or table+size*count>len(elf):
        raise ValueError('Invalid ELF section table')
    ranges=[]
    for i in range(count):
        at=table+i*size;kind=struct.unpack_from('<I',elf,at+4)[0]
        flags,address,offset,length=struct.unpack_from('<QQQQ',elf,at+8)
        if flags&3!=3 or not length:continue
        if kind not in (1,8) or address<0x100000 or address+length>0x400000:
            raise ValueError('Unsupported writable target section')
        if kind==1 and offset+length>len(elf):raise ValueError('Truncated target data')
        ranges.append((address,length))
    ranges.sort()
    if any(a+n>b for (a,n),(b,m) in zip(ranges,ranges[1:])):
        raise ValueError('Overlapping writable target sections')
    return ranges


class ReusingEvaluator(Evaluator):
    def __init__(self,path,deps):
        super().__init__(path,deps)
        # The base verifier checked the ELF digest as well as probe.bin.
        elf_path=self.path.parent/'probe.elf'
        if 'probe.elf' not in self.report['generated_sha256']:raise ValueError('Missing verified ELF')
        self.ranges=writable_ranges(elf_path.read_bytes())
        self.arm=self.Uc(self.arch,self.mode)
        self.arm.mem_map(0x100000,0x300000);self.arm.mem_write(0x100000,self.binary)
        self.arm.mem_map(0x800000,0x10000)
        self.initial=[(at,bytes(self.arm.mem_read(at,n))) for at,n in self.ranges]
        self.context=self.arm.context_save();self.lock=threading.Lock()
        self.report=dict(self.report,source_sha256={**self.report['source_sha256'],
            str(Path(__file__).resolve()):sha(Path(__file__))})

    def evaluate(self,request):
        if len(request)!=88:raise ValueError('Require exactly one canonical target-math request')
        with self.lock:
            arm=self.arm;ar=self.ar
            arm.context_restore(self.context)
            for at,data in self.initial:arm.mem_write(at,data)
            arm.mem_write(0x800000,bytes(0x10000))
            arm.mem_write(0x800100,request);arm.mem_write(0x800200,b'\xa5'*102)
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

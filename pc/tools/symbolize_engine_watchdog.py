"""Symbolize private watchdog offsets only against the exact accepted NRO ELF."""
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''):h.update(b)
 return h.hexdigest()


def run(a):
 proof=json.loads(a.validation.read_text());elf=a.elf.resolve();out=a.output.resolve()
 if proof.get('status')!='BUILT' or sha(elf)!=proof.get('elf_sha256'):raise ValueError('ELF differs from accepted NRO build')
 if out.exists():raise ValueError('Use a fresh output directory')
 with elf.open('rb') as f:
  h=f.read(64)
  if h[:6]!=b'\x7fELF\x02\x01':raise ValueError('Require ELF64 little endian')
  phoff=struct.unpack_from('<Q',h,32)[0];size,count=struct.unpack_from('<HH',h,54);ranges=[]
  if size!=56:raise ValueError('Unexpected program-header size')
  for i in range(count):
   f.seek(phoff+i*size);row=struct.unpack('<IIQQQQQQ',f.read(size))
   if row[0]==1:ranges.append((row[3],row[3]+row[6]))
 text=a.log.read_text(errors='replace');offsets=sorted({int(s,16) for s in re.findall(r'(?:pc_offset|lr_offset|return_offset)=([0-9A-Fa-f]{16})',text)})
 accepted=[n for n in offsets if any(lo<=n<hi for lo,hi in ranges)];rejected=[n for n in offsets if n not in accepted]
 if not accepted:raise ValueError('No offsets inside accepted ELF load ranges')
 symbols=subprocess.check_output([str(a.nm.resolve()),'--defined-only','--format=posix',str(elf)],text=True)
 functions={}
 for line in symbols.splitlines():
  fields=line.split()
  if len(fields)<3 or fields[1] not in ('T','t','W','w'):continue
  start=int(fields[2],16);size=int(fields[3],16) if len(fields)>3 else 0
  functions.setdefault(start,[]).append((fields[0],size))
 starts=sorted(functions);resolved=[]
 for offset in accepted:
  index=bisect.bisect_right(starts,offset)-1;aliases=[];delta=0
  if index>=0:
   start=starts[index];delta=offset-start
   aliases=[name for name,size in functions[start] if delta==0 or delta<size]
  resolved.append(dict(offset=hex(offset),symbols=sorted(aliases),displacement=delta if aliases else None))
 result=subprocess.run([str(a.addr2line.resolve()),'-a','-f','-C','-e',str(elf)],input=''.join(hex(n)+'\n' for n in accepted),capture_output=True,text=True,check=True)
 out.mkdir(parents=True)
 (out/'symbols.json').write_text(json.dumps(resolved,indent=2)+'\n')
 (out/'symbols.txt').write_text('ELF symbol table (DWARF lines are separate advisory data):\n'+''.join(row['offset']+' '+(', '.join(row['symbols']) or '<no covering text symbol>')+' +'+str(row['displacement'])+'\n' for row in resolved))
 (out/'addr2line.txt').write_text(result.stdout)
 report=dict(status='SYMBOLIZED',build=proof['build'],elf_sha256=sha(elf),log_sha256=sha(a.log),offsets=len(accepted),rejected_outside_image=[hex(n) for n in rejected],hardware_executed_by_this_tool=False)
 (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for k in ('validation','elf','log','nm','addr2line','output'):p.add_argument('--'+k,type=Path,required=True)
 run(p.parse_args())

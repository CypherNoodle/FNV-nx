"""Check exact-build watchdog symbolization and reject a different ELF identity."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def run(a):
 out=a.output.resolve();pc=Path(__file__).resolve().parents[1];tool=pc/'tools/symbolize_engine_watchdog.py'
 if out.exists():raise ValueError('Use a fresh output directory')
 out.mkdir(parents=True)
 rows=subprocess.check_output([str(a.nm),'--defined-only','--format=posix',str(a.elf)],text=True)
 start=next(row.split() for row in rows.splitlines() if row.startswith('_start '));offset=int(start[2],16)
 log=out/'own-offsets.log';log.write_text(f'pc_offset={offset:016X} lr_offset=FFFFFFFFFFFFFFFF\n')
 command=[sys.executable,str(tool),'--validation',str(a.validation),'--elf',str(a.elf),'--log',str(log),'--nm',str(a.nm),'--addr2line',str(a.addr2line),'--output',str(out/'positive')]
 subprocess.run(command,check=True,capture_output=True,text=True)
 symbols=json.loads((out/'positive/symbols.json').read_text());assert len(symbols)==1 and '_start' in symbols[0]['symbols']
 proof=json.loads(a.validation.read_text());proof['elf_sha256']='0'*64
 wrong=out/'wrong-validation.json';wrong.write_text(json.dumps(proof));command[command.index('--validation')+1]=str(wrong);command[command.index('--output')+1]=str(out/'rejected')
 result=subprocess.run(command,capture_output=True,text=True)
 assert result.returncode and 'ELF differs from accepted NRO build' in result.stderr and not (out/'rejected').exists()
 paths=[Path(__file__),tool,a.validation,a.nm,a.addr2line]
 report=dict(status='PASS',own_start_symbol=True,outside_offset_rejected=True,wrong_elf_identity_rejected=True,dwarf_advisory_only=True,source_sha256={str(p.resolve()):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths})
 (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:v for k,v in report.items() if k!='source_sha256'}))


if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for k in ('validation','elf','nm','addr2line','output'):p.add_argument('--'+k,type=Path,required=True)
 run(p.parse_args())

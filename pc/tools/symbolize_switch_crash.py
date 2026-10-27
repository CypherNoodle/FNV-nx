"""Symbolize the first Atmosphere crashed thread with its exact private ELF.

Reject a mismatched ELF Build ID before looking up addresses. Output can contain
private game function names and is not a publication artifact.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
from link_switch_engine_platform import sha

def parse_report(text):
    first=text.split('Thread Report:',1)[0]
    module=re.search(r'Module Id:\s*([0-9A-Fa-f]{64})',first)
    if not module:raise ValueError('Missing complete Atmosphere module identity')
    addresses=[]
    pattern=r'^\s*(PC|LR|ReturnAddress\[\d+\]):\s*[0-9A-Fa-f]+\s*\(fnv-nx \+ 0x([0-9A-Fa-f]+)\)'
    for match in re.finditer(pattern,first,re.M):addresses.append(dict(label=match[1],offset=int(match[2],16)))
    if not any(row['label']=='PC' for row in addresses):raise ValueError('Missing crashed-thread PC offset')
    return module[1].lower(),addresses

def symbolize(a):
    if a.output.exists():raise ValueError('Use a fresh report path')
    frozen={str(p.resolve()):sha(p) for p in (a.log,a.elf,a.readelf,a.addr2line,Path(__file__))}
    identity,addresses=parse_report(a.log.read_text())
    notes=subprocess.check_output([str(a.readelf),'-n',str(a.elf)],text=True)
    match=re.search(r'Build ID:\s*([0-9a-fA-F]+)',notes)
    if not match or match[1].lower().ljust(64,'0')!=identity:raise ValueError('Crash report does not belong to this ELF Build ID')
    output=subprocess.check_output([str(a.addr2line),'-f','-C','-e',str(a.elf),*[hex(row['offset']) for row in addresses]],text=True).splitlines()
    if len(output)!=2*len(addresses):raise ValueError('Unexpected symbolizer output')
    for i,row in enumerate(addresses):row.update(function=output[i*2],location=output[i*2+1])
    if any(sha(Path(p))!=value for p,value in frozen.items()):raise ValueError('Inputs changed during symbolization')
    result=dict(status='SYMBOLIZED',build_id=match[1].lower(),crashed_thread=addresses,source_sha256=frozen,
                cause_proven=False,limitation='Function/line lookup does not establish the source of an invalid guest pointer.')
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('log','elf','readelf','addr2line','output'):parser.add_argument('--'+name,type=Path,required=True)
    r=symbolize(parser.parse_args());print(json.dumps(dict(status=r['status'],crashed_thread=r['crashed_thread'])))

"""Whole-body EH3 helper audit controls and an optional complete fixture pipeline."""
import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import struct
import sys
from pe_fixture import write_pe
from test_windows_eh3_frames import programs
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_eh3_helpers import describe,verify
from prepare_aot_image import prepare
from run_aot_analysis import run,sha


def test(a):
    if a.output.exists():raise ValueError('Use a fresh private directory')
    sys.path.insert(0,str(a.python_deps.resolve()));from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    a.output.mkdir(parents=True);root=a.output.resolve();dummy=bytearray(4096);pairs=programs(dummy)
    prolog=pairs[0x401900].replace(struct.pack('<I',0x401a20),struct.pack('<I',0x402000));epilog=pairs[0x401960]
    def fixture(name,p,e):
        folder=root/name;folder.mkdir();exe=folder/'fixture.exe';write_pe(exe);raw=bytearray(exe.read_bytes());raw.extend(bytes(512))
        struct.pack_into('<H',raw,0x86,2);struct.pack_into('<I',raw,0x98+56,0x3000);struct.pack_into('<I',raw,0x98+224+36,0x60000020)
        table=0x98+224+40;raw[table:table+8]=b'.data\0\0\0';struct.pack_into('<4I',raw,table+8,512,0x2000,512,4096)
        raw[0x700:0x700+len(p)]=p;raw[0x780:0x780+len(e)]=e;struct.pack_into('<I',raw,0x98+16,0x1400)
        code=bytearray(b'\x6a\x20\xb8'+struct.pack('<I',0x401900))
        for target in (0x401500,0x401580):code+=b'\xe8'+struct.pack('<i',target-(0x401400+len(code)+5))
        code+=b'\xc3';raw[0x600:0x600+len(code)]=code;raw[0xb00]=0xc3;exe.write_bytes(raw)
        analysis=folder/'analysis';prepare(exe,analysis,sha(exe));db=analysis/'disasm';db.mkdir()
        (db/'functions.json').write_text(json.dumps([dict(start=hex(at),end=at+size,section='.text') for at,size in [(0x401500,len(p)),(0x401580,len(e))]]))
        return analysis
    analysis=fixture('positive',prolog,epilog);report=describe(analysis,0x401500,0x401580);assert verify(analysis,report)==report;checks=1
    for key in ('body_sha256','address','publish_address','bytes'):
        changed=deepcopy(report);changed['helpers'][0][key]=0
        try:verify(analysis,changed)
        except ValueError:checks+=1
        else:raise AssertionError('Changed helper proof accepted')
    for kind,code in [('prolog',prolog),('epilog',epilog)]:
        rows=list(Cs(CS_ARCH_X86,CS_MODE_32).disasm(code,0x401500))
        for i in range(len(rows)):
            mutant=b''.join(bytes(x.bytes) for j,x in enumerate(rows) if j!=i)
            trial=fixture(kind+'-delete-'+str(i),mutant if kind=='prolog' else prolog,mutant if kind=='epilog' else epilog)
            try:describe(trial,0x401500,0x401580)
            except ValueError:checks+=1
            else:raise AssertionError('Deleted instruction accepted')
    generation=None
    if a.sdk:
        proof=root/'helpers.json';proof.write_text(json.dumps(report))
        generation=run(analysis,a.sdk,a.python_deps,stage='all',generation_name='eh3-fixture',frame_restore=True,eh3_helpers=proof)
        assert generation['eh3_frame_overlay']['audit']==report and generation['full_selection'] and not generation['gaps']['failed_functions']
        assert generation['status']=='GENERATED_UNVALIDATED'
    result=dict(status='PASS',checks=checks,pipeline_generated=bool(generation),native_engine_started=False,
                source_sha256={str(p):sha(p) for p in [Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'tools/audit_eh3_helpers.py',Path(__file__).resolve().parents[1]/'tools/aot_eh3_frames.py',Path(__file__).resolve().parents[1]/'tools/run_aot_analysis.py']})
    (root/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('python-deps','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--sdk',type=Path);test(p.parse_args())

"""Synthetic static gate recognition and rejection; no game data or execution."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
from pe_fixture import write_pe
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_d3d9_startup_requirements import audit


def run(a):
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);base=0x401000;capture=bytearray(b'\x90'*0xe00);text='Synthetic renderer gate'
    rawfile=out/'fixture.exe';write_pe(rawfile);raw=bytearray(rawfile.read_bytes());string_va=base+0xd00
    at=0x100
    def emit(b):
        nonlocal at
        capture[at:at+len(b)]=b;at+=len(b)
    def rel(op,target):
        emit(op+struct.pack('<i',target-(base+at+len(op)+4)))
    emit(bytes.fromhex('8b481c81e10000010085c9'));rel(b'\x0f\x84',base+0x300)
    for field,helper in ((196,0x500),(204,0x520)):
        emit(b'\x8b\xb0'+struct.pack('<I',field)+bytes.fromhex('81e6ffff0000'));rel(b'\xe8',base+helper);emit(bytes.fromhex('39c6'));rel(b'\x0f\x82',base+0x2f0)
    capture[0x300:0x306]=b'\x68'+struct.pack('<I',string_va)+b'\xc3'
    for helper,slot in ((0x500,0xc00),(0x520,0xc04)):
        capture[helper:helper+10]=bytes.fromhex('558beca1')+struct.pack('<I',base+slot)+bytes.fromhex('5dc3')
    capture[0xd00:0xd00+len(text)+1]=text.encode()+b'\0';struct.pack_into('<II',capture,0xc00,0x201,0x300)
    raw[0x200:]=capture;rawfile.write_bytes(raw);capfile=out/'capture.bin';capfile.write_bytes(capture)
    sha=lambda b:hashlib.sha256(b).hexdigest()
    def check(rb,cb,eh=None,ch=None):
        rawfile.write_bytes(rb);capfile.write_bytes(cb)
        return audit(rawfile,capfile,eh or sha(rb),ch or sha(cb),base,text,a.python_deps)
    result=check(raw,capture);gate=result['gate'];assert gate['required_mask']==65536 and [r['initial_required_version'] for r in gate['shader_version_checks']]==[0x201,0x300]
    rejected=0
    def reject(rb,cb,eh=None,ch=None):
        nonlocal rejected
        try:check(rb,cb,eh,ch)
        except ValueError:rejected+=1
        else:raise AssertionError('invalid gate accepted')
    reject(raw,capture,'0'*64);reject(raw,capture,ch='0'*64)
    b=capture.copy();b[0x107]=2;reject(raw,b)
    b=capture.copy();b[0x50a]=0x40;b[0x509]=0x40;reject(raw,b)
    b=capture.copy();b[0x350:0x355]=b'\x68'+struct.pack('<I',string_va);reject(raw,b)
    b=capture.copy();b[0x10d]=0x85;reject(raw,b)
    b=capture.copy();b[0x114]=195;reject(raw,b)
    b=raw.copy();b[0x200+0xd80:0x200+0xd80+len(text)+1]=text.encode()+b'\0';reject(b,capture)
    rawfile.write_bytes(raw);capfile.write_bytes(capture)
    report=dict(status='PASS',synthetic_gate_checks=1,negative_checks=rejected,game_data_used=False,code_executed=False,
        source_sha256={str(p.resolve()):sha(p.read_bytes()) for p in (Path(__file__),Path(__file__).with_name('pe_fixture.py'),Path(__file__).parents[1]/'tools/audit_d3d9_startup_requirements.py')})
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--python-deps',type=Path,required=True);run(p.parse_args())

"""Identity, complete-body and mutation controls for normal-frame helper audit."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
from pe_fixture import write_pe
from seh_cookie_fixture import helpers
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_seh_helpers import body,classify,describe,verify,generation_arguments
from prepare_aot_image import prepare


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--python-deps',type=Path,required=True)
    p.add_argument('--sdk',type=Path,help='Also exercise the complete named-generation pipeline on this own fixture')
    a=p.parse_args();sys.path.insert(0,str(a.python_deps.resolve()))
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True
    rejected=0
    for handler,cookie in [(0x401900,0x402000),(0x401910,0x402004)]:
        prolog,epilog=helpers(handler,cookie)
        for kind,code in [('prolog',prolog),('epilog',epilog)]:
            rows=list(decoder.disasm(code,0x401500));assert classify(rows,kind)['kind']==kind
            # Every instruction is required; no permissive marker-only matching.
            for i in range(len(rows)):
                try:classify(rows[:i]+rows[i+1:],kind)
                except ValueError:rejected+=1
                else:raise AssertionError('Deleted instruction accepted')
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);exe=root/'fixture.exe';write_pe(exe);raw=bytearray(exe.read_bytes())
            raw.extend(bytes(512));struct.pack_into('<H',raw,0x86,2);struct.pack_into('<I',raw,0x98+56,0x3000)
            struct.pack_into('<I',raw,0x98+224+36,0x60000020)
            table=0x98+224+40;raw[table:table+8]=b'.data\0\0\0'
            struct.pack_into('<4I',raw,table+8,512,0x2000,512,4096)
            raw[0x700:0x700+len(prolog)]=prolog;raw[0x780:0x780+len(epilog)]=epilog
            struct.pack_into('<I',raw,0x98+16,0x1400)
            caller=bytearray(b'\x6a\x20\x68'+struct.pack('<I',handler))
            for target in (0x401500,0x401580):
                caller+=b'\xe8'+struct.pack('<i',target-(0x401400+len(caller)+5))
            caller+=b'\xc3';raw[0x600:0x600+len(caller)]=caller
            raw[handler-0x401000+0x200]=0xc3
            exe.write_bytes(raw);analysis=root/'analysis';prepare(exe,analysis,hashlib.sha256(raw).hexdigest())
            report=describe(analysis,0x401500,0x401580);assert verify(analysis,report)==report
            assert generation_arguments(analysis,report)==['--seh-prolog','0x401500','--seh-epilog','0x401580']
            if a.sdk and handler==0x401900:
                from run_aot_analysis import run
                proof=root/'helpers.json';proof.write_text(json.dumps(report))
                generated=run(analysis,a.sdk,a.python_deps,stage='all',generation_name='cookie-test',
                              frame_restore=True,seh_helpers=proof)
                assert generated['normal_frame_helpers']==report and generated['full_selection']
                assert generated['functions_generated']>=3 and not generated['gaps']['failed_functions']
                assert generated['status']=='GENERATED_UNVALIDATED'
            for key in ('body_sha256','address','bytes'):
                changed=copy.deepcopy(report);changed['helpers'][0][key]=0
                try:verify(analysis,changed)
                except ValueError:rejected+=1
                else:raise AssertionError('Changed helper proof accepted')
            for start in (0x400000,0x401501,0x402000):
                try:describe(analysis,start,0x401580)
                except ValueError:rejected+=1
                else:raise AssertionError('Wrong helper root accepted')
            image=(analysis/'fnv-analysis.xbe').read_bytes()
            try:body(image[:-1],0x400000,0x401500)
            except ValueError:rejected+=1
            else:raise AssertionError('Truncated container accepted')
    print(json.dumps(dict(status='PASS',negative_controls=rejected,engine_started=False)))

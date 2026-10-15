"""Verify analysis-container addresses and codegen gap detection, without execution."""
import hashlib
import argparse
import json
import struct
import sys
import tempfile
from pathlib import Path
from pe_fixture import write_pe
pc=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(pc/'tools'))
from prepare_aot_image import prepare
from run_aot_analysis import generation_gaps, generation_paths, verified_scan_wrapper, run

def test(temp_dir=None):
    with tempfile.TemporaryDirectory(prefix='fnv-aot-input-',dir=temp_dir) as tmp:
        root=Path(tmp); exe=root/'fixture.exe'; out=root/'analysis'
        write_pe(exe,('KERNEL32.dll',))
        raw=bytearray(exe.read_bytes()); optional=0x98; table=optional+224
        # Eight sections like FNV; an executable .text plus separate TLS data.
        raw += bytes(0x3000-len(raw))
        struct.pack_into('<H',raw,0x86,8)
        struct.pack_into('<II',raw,optional+56,0x9000,0x400)
        struct.pack_into('<IIII',raw,table+8,0xe00,0x1000,0xe00,0x400)
        raw[0x400:0x1200]=exe.read_bytes()[0x200:]
        for i in range(8):
            name='.text' if i==0 else ('.tls' if i==7 else '.d'+str(i))
            raw[table+i*40:table+i*40+8]=name.encode().ljust(8,b'\0')
            struct.pack_into('<IIII',raw,table+i*40+8,
                0xe00 if i==0 else 0x300,(i+1)*0x1000,0xe00 if i==0 else 0x200,
                0x400 if i==0 else 0x1200+(i-1)*0x200)
            struct.pack_into('<I',raw,table+i*40+36,0x60000020 if i==0 else 0xc0000040)
        struct.pack_into('<II',raw,optional+168,0x8000,24)
        exe.write_bytes(raw); digest=hashlib.sha256(raw).hexdigest()
        report=prepare(exe,out,digest)
        assert report['tls_directory_rva']==0x8000 and not report['cold_boot_supported']
        xbe=(out/'fnv-analysis.xbe').read_bytes()
        base,headers,image_size,_,_,cert,count,table_va=struct.unpack_from('<8I',xbe,0x104)
        assert base==0x400000 and headers==4096 and image_size==0x9000 and count==8
        assert (struct.unpack_from('<I',xbe,0x128)[0]^0xa8fc57ab)==0x401000
        def cstr(offset): return xbe[offset:xbe.index(0,offset)].decode()
        for i in range(count):
            flags,va,vs,offset,size,name,*_=struct.unpack_from('<9I',xbe,table_va-base+i*56)
            assert va==base+(i+1)*0x1000 and size<=vs
            assert name-base>=table_va-base+count*56
            assert cstr(name-base)==report['sections'][i]['name']
            source_offset=0x400 if i==0 else 0x1200+(i-1)*0x200
            assert xbe[offset:offset+size]==raw[source_offset:source_offset+size]
        before=exe.read_bytes(); code=root/'text.bin'; code.write_bytes(bytes([0x90])*0xe00)
        code_sha=hashlib.sha256(code.read_bytes()).hexdigest()
        report=prepare(exe,out,digest,code,code_sha,0x401010)
        assert report['code_source']=='captured-loaded-text' and report['analysis_entry']==0x401010
        assert exe.read_bytes()==before and not report['original_startup_entry_verified']
        for args in ((digest,code,None,0x401010),(digest,code,'0'*64,0x401010),
                     (digest,code,code_sha,0x402000),('0'*64,None,None,None)):
            try: prepare(exe,out,*args)
            except ValueError: pass
            else: raise AssertionError('Unverified input accepted')
        # Entry in .bind must not be treated as a plain-code startup profile.
        raw[table:table+8]=b'.bind\0\0\0'; exe.write_bytes(raw)
        try: prepare(exe,out,hashlib.sha256(raw).hexdigest())
        except ValueError: pass
        else: raise AssertionError('Packed startup accepted')
        generated=root/'generated'; generated.mkdir()
        (generated/'chunk.c').write_text('void f(void) { /* translation failed */ }\n'
            'recomp_unimpl("missing", 0); /* foo: unhandled */')
        gaps=generation_gaps({'failed':1,'unresolved_stubs':2,'unimplemented':{'daa':3}},generated)
        assert gaps['failed_functions']==1 and gaps['unresolved_call_targets']==2
        assert gaps['unsupported_instructions']=={'daa':3}
        assert list(gaps['code_markers'].values())==[1,1,1]
        # Each named generation must isolate every artifact, not only C files.
        baseline=generation_paths(root)
        variant=generation_paths(root,'bits01')
        assert all(variant[k]!=baseline[k] and variant[k].parent==root for k in baseline)
        for name in ('../escape','Bits','a/b','a\\b','','a'*49):
            try: generation_paths(root,name)
            except ValueError: pass
            else: raise AssertionError('Unsafe generation name accepted')
        wrapper=root/'current-index.py'; wrapper.write_text('version=2\n')
        archived=root/'aot-index-used.py'
        archived.write_text('raise RuntimeError("identity evidence must never execute")\n')
        fingerprint=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
        assert verified_scan_wrapper(root,{'index_wrapper_sha256':fingerprint(wrapper)},wrapper)==wrapper
        archived_sha=fingerprint(archived)
        assert verified_scan_wrapper(root,{'index_wrapper_sha256':archived_sha},wrapper)==archived
        archived.write_text('altered archive\n')
        try: verified_scan_wrapper(root,{'index_wrapper_sha256':archived_sha},wrapper)
        except ValueError: pass
        else: raise AssertionError('Changed archived wrapper accepted')
        # Fail before running any SDK stage or reading a missing input profile.
        accepted=generated/'chunk.c'; accepted_bytes=accepted.read_bytes()
        try: run(root,root/'missing-sdk',stage='all')
        except ValueError as e: assert 'Generation output already exists' in str(e)
        else: raise AssertionError('Existing output accepted')
        assert accepted.read_bytes()==accepted_bytes
        print('PASS: eight-section container, TLS metadata, exact code identity, unchanged EXE, packed-entry rejection, codegen gaps; no guest execution')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--temp-dir',type=Path)
    test(parser.parse_args().temp_dir)

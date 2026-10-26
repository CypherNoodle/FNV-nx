"""Check adapter identity fixtures against the actual Windows INI reader."""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from prepare_startup_display_fixture import prepare


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    reader=C.WinDLL('kernel32',use_last_error=True).GetPrivateProfileStringW
    reader.argtypes=[C.c_wchar_p,C.c_wchar_p,C.c_wchar_p,C.c_wchar_p,C.c_uint32,C.c_wchar_p];reader.restype=C.c_uint32
    names=['FNV-nx NVK','Mesa llvmpipe (LLVM 19.1.7, 128 bits)','GPU A=B #1; Adapter','A'*255]
    for i,name in enumerate(names):
        report=prepare(out/str(i),1280,720,name);path=out/str(i)/report['file']
        assert hashlib.sha256(path.read_bytes()).hexdigest()==report['sha256']
        for key,wanted in [('sD3DDevice',name),('iSize W','1280'),('iSize H','720'),('bFull Screen','1')]:
            buf=C.create_unicode_buffer(1024);n=reader('Display',key,'missing',buf,len(buf),str(path))
            assert buf.value==wanted and n==len(wanted),(key,buf.value,wanted)
    baseline=prepare(out/'legacy',1280,720)
    assert (out/'legacy'/baseline['file']).read_bytes()==b'[Display]\niSize W=1280\niSize H=720\nbFull Screen=1\n'
    bad=['', ' space','space ','a\nb','a\rb','a\0b','GPU\u00e9','"quoted"','"first','last"','a'*256,4]
    for i,name in enumerate(bad):
        target=out/('invalid-'+str(i))
        try:prepare(target,1280,720,name)
        except ValueError:pass
        else:raise AssertionError('Malformed adapter accepted')
        assert not target.exists()
    path=out/'0'/baseline['file'];before=path.read_bytes()
    try:prepare(out/'0',1280,720,'Other GPU')
    except FileExistsError:pass
    else:raise AssertionError('Existing fixture overwritten')
    assert path.read_bytes()==before
    result=dict(status='PASS',windows_ini_checks=len(names)*4,invalid_adapters=len(bad),
                legacy_bytes_preserved=True,overwrite_rejected=True,hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':main()

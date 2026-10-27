"""Verify actual flat ROMFS bounds and hashes using synthetic metadata/data."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from stage_switch_engine_probe import verify_embedded_files

with tempfile.TemporaryDirectory(prefix='engine-romfs-',dir=Path.cwd()) as tmp:
    path=Path(tmp)/'own.nro';name=b'own.bin';payload=b'own synthetic data'
    root=struct.pack('<6I',0xffffffff,0xffffffff,0xffffffff,0,0xffffffff,0)
    file=struct.pack('<IIQQII',0,0xffffffff,0,len(payload),0xffffffff,len(name))+name
    header=struct.pack('<10Q',80,80,0,80,len(root),104,0,104,len(file),104+len(file))
    rom=header+root+file+payload
    nro=bytearray(128);nro[16:20]=b'NRO0';struct.pack_into('<I',nro,24,128)
    asset=bytearray(56);asset[:4]=b'ASET';struct.pack_into('<QQ',asset,40,56,len(rom))
    valid=bytes(nro)+bytes(asset)+rom;path.write_bytes(valid)
    expected={'own.bin':hashlib.sha256(payload).hexdigest()}
    assert verify_embedded_files(path,expected)==expected
    broken=[]
    for at,value in [(16,0),(128,0),(184+80+8,0),(184+104+4,0)]:
        blob=bytearray(valid);struct.pack_into('<I',blob,at,value);broken.append(bytes(blob))
    broken.extend([valid[:-1],valid[:-len(payload)]+b'X'*len(payload)])
    for blob in broken:
        path.write_bytes(blob)
        try:verify_embedded_files(path,expected)
        except ValueError:pass
        else:raise AssertionError('Invalid ROMFS accepted')
print('PASS flat ROMFS: content hashes, truncated bounds, directory/file cycles and invalid headers')

"""Boundary tests for the own SDP reader. No proprietary fixtures."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from shader_packages import parse_package

def fixture(name=b'test.vso',code=None):
    if code is None:code=struct.pack('<2I',0xFFFE0300,0xFFFF)
    payload=name.ljust(256,b'\0')+struct.pack('<I',len(code))+code
    return struct.pack('<3I',100,1,len(payload))+payload

class Packages(unittest.TestCase):
    def test_roundtrip(self):
        data=fixture();r=parse_package(data);self.assertEqual(len(r),1)
        self.assertEqual(r[0]['name'],'test.vso');self.assertEqual(r[0]['stage'],'vertex')
        self.assertEqual(data[r[0]['offset']:],r[0]['code'])
    def test_rejects(self):
        data=fixture();variants=[b'',data[:11],data[:-1],data+b'x',fixture(b'x'*256),fixture(b''),fixture(b'\x01')]
        for offset,value in ((0,101),(4,0xFFFFFFFF),(8,0),(268,7),(268,0xFFFFFFFC),(272,0xFFFE0400),(272,0xFFFD0300),(276,0)):
            raw=bytearray(data);struct.pack_into('<I',raw,offset,value);variants.append(bytes(raw))
        for raw in variants:
            with self.subTest(bytes=len(raw)):
                with self.assertRaises(ValueError):parse_package(raw)
    def test_exports_do_not_use_shader_name_as_path(self):
        self.assertEqual(parse_package(fixture(b'../../example.vso'))[0]['name'],'../../example.vso')

if __name__=='__main__':unittest.main()

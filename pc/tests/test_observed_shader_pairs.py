"""Check declaration and register evidence used to prepare observed pairs."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from prepare_observed_shader_pairs import declaration,vertex_semantics,input_mappings

END=b'\xff\0\0\0\x11\0\0\0'
def element(usage,index=0,kind=2):return struct.pack('<HHBBBB',0,0,kind,0,usage,index)
def shader(*decls):
    words=[0xfffe0300]
    for register,usage,index in decls:words.extend((0x0200001f,0x80000000|usage|(index<<16),0x900f0000|register))
    words.append(0xffff);return struct.pack('<%dI'%len(words),*words)

class ObservedPairs(unittest.TestCase):
    def test_exact_evidence(self):
        semantics=vertex_semantics(shader((0,0,0),(3,5,2)))
        self.assertEqual(semantics,{0:(0,0),3:(5,2)})
        self.assertEqual(declaration(element(0)+element(5,2,1)+END),{(0,0):2,(5,2):1})
        reflected={'interfaces':[{'storage':1,'name':'vs_v3','location':7},{'storage':3,'name':'vs_oPos','builtin':0},{'storage':1,'name':'vs_v0','location':1}]}
        self.assertEqual(input_mappings(reflected,semantics),[[5,2,7],[0,0,1]])

    def test_bad_declarations(self):
        for data in (b'',END,element(0),element(0)+END+b'\0',element(0)+element(0)+END,element(0,kind=8)+END,element(0,kind=17)+END):
            with self.subTest(data=data.hex()),self.assertRaises(ValueError):declaration(data)

    def test_explicit_legacy_declarations(self):
        words=[0xfffe0101,0x0002fffe,31,0xffff,81,0xa00f0000,0,0,0,0,
               31,0x80000000,0x900f0000,31,0x80000005,0x900f0003,0xffff]
        code=struct.pack('<%dI'%len(words),*words)
        self.assertEqual(vertex_semantics(code),{0:(0,0),3:(5,0)})
        for bad in (words[:-2],words+[0],words[:-1]+[0x0200001f,0,0,0xffff],words[:-1]+[0xdead,0xffff]):
            with self.subTest(words=bad),self.assertRaises(ValueError):vertex_semantics(struct.pack('<%dI'%len(bad),*bad))

    def test_bad_bytecode(self):
        for data in (b'',shader((0,0,0))[:-4],shader((0,0,0),(0,5,0)),shader((0,0,0),(1,0,0)),struct.pack('<2I',0xfffe0101,0xffff),struct.pack('<2I',0xffff0300,0xffff)):
            with self.subTest(data=data.hex()),self.assertRaises(ValueError):vertex_semantics(data)

    def test_no_inferred_mapping(self):
        for entries in ([{'storage':1,'name':'vs_v9','location':0}],[{'storage':1,'name':'unknown','location':0}],[{'storage':1,'name':'vs_v0'}],[{'storage':1,'name':'vs_v0','location':1},{'storage':1,'name':'vs_v1','location':1}]):
            with self.subTest(entries=entries),self.assertRaises(ValueError):input_mappings({'interfaces':entries},{0:(0,0),1:(5,0)})

if __name__=='__main__':unittest.main()

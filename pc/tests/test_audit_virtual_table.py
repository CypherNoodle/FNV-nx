"""Synthetic PE table bounds, interior labels and gap audit selection."""
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_virtual_table import audit_table,table_offset

def test():
    image=bytearray(0x400);image[:2]=b'MZ';struct.pack_into('<I',image,0x3c,0x80);image[0x80:0x84]=b'PE\0\0'
    struct.pack_into('<HH',image,0x84,0x14c,1);struct.pack_into('<H',image,0x94,0xe0);struct.pack_into('<H',image,0x98,0x10b);struct.pack_into('<I',image,0x98+28,0x400000)
    section=0x178;struct.pack_into('<IIII',image,section+8,0x200,0x2000,0x200,0x200);struct.pack_into('<I',image,section+36,0x40000040)
    struct.pack_into('<4I',image,0x200,0x401000,0x401001,0x401004,0)
    functions=[dict(start=0x401000,end=0x401002)];r=audit_table(image,0x402000,4,b'\x90\xc3\xcc\xcc\xc3',0x401000,functions)
    assert r['known_entry_slots']==1 and r['interior_targets']==[dict(entry=0x401001,slots=[1])] and r['outside_code_slots']==[3]
    assert len(r['audited_gaps'])==1 and r['audited_gaps'][0]['entry']==0x401004
    bad=[]
    bad.append((bytes(64),0x402000,4));bad.append((image,0x402001,1));bad.append((image,0x402000,0));bad.append((image,0x402000,4097));bad.append((image,0x4021fc,2))
    mutable=bytearray(image);struct.pack_into('<I',mutable,section+36,0xc0000040);bad.append((mutable,0x402000,4))
    executable=bytearray(image);struct.pack_into('<I',executable,section+36,0x60000020);bad.append((executable,0x402000,4))
    for data,address,count in bad:
        try:table_offset(data,address,count)
        except ValueError:pass
        else:raise AssertionError('Invalid table accepted')
    for functions in ([dict(start=1,end=1)],[dict(start=-1,end=1)],[dict(start=1,end=0x100000001)]):
        try:audit_table(image,0x402000,4,b'\xc3',0x401000,functions)
        except ValueError:pass
        else:raise AssertionError('Malformed function range accepted')
    truncated=bytearray(image[:0x200]);struct.pack_into('<H',truncated,0x86,4)
    try:table_offset(truncated,0x402000,1)
    except ValueError:pass
    else:raise AssertionError('Truncated section table accepted')
    print('PASS explicit PE table, interior rejection, gap selection and eleven invalid inputs')

if __name__=='__main__':test()

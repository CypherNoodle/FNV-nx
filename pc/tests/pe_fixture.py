"""Synthetic PE32 headers/imports for parser tests. Never executed."""
import struct
def write_pe(path, dlls=(), section=".text"):
    raw=bytearray(4096)
    def put(at,fmt,*values): struct.pack_into("<"+fmt,raw,at,*values)
    raw[:2]=b"MZ"; put(60,"I",0x80)
    raw[0x80:0x84]=b"PE\0\0"; put(0x84,"HH",0x14c,1); put(0x94,"H",224)
    optional=0x98; put(optional,"H",0x10b); put(optional+16,"I",0x1000)
    put(optional+28,"I",0x400000); put(optional+32,"II",0x1000,0x200)
    put(optional+56,"II",0x2000,0x200); put(optional+92,"I",16)
    table=optional+224
    raw[table:table+8]=section.encode().ljust(8,b"\0")
    put(table+8,"IIII",len(raw)-0x200,0x1000,len(raw)-0x200,0x200)
    if dlls:
        directory=0x100; cursor=0x100+(len(dlls)+1)*20
        put(optional+104,"II",0x1000+directory,(len(dlls)+1)*20)
        for i,dll in enumerate(dlls):
            name=cursor; data=dll.encode()+b"\0"; raw[0x200+cursor:0x200+cursor+len(data)]=data
            cursor=(cursor+len(data)+3)&~3; thunk=cursor
            put(0x200+thunk,"II",0x80000001,0); cursor+=8
            put(0x200+directory+i*20,"IIIII",0x1000+thunk,0,0,0x1000+name,0x1000+thunk)
    path.write_bytes(raw)

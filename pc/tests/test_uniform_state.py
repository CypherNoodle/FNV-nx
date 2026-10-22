"""CPU-only bank/bind/version-ring model, fault rejection and thread checks."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import ctypes as C
import hashlib
import json
from pathlib import Path
import subprocess
from test_uniform_pack import Entry,Plan,U32

U64=C.c_uint64
class Config(C.Structure):
    _fields_=[(n,U32) for n in ('vertex_floats','pixel_floats','slots','max_bytes')]
class Draw(C.Structure):
    _fields_=[('ticket',U64),('data',C.c_void_p*2),('bytes',U32*2)]
class Stats(C.Structure):
    _fields_=[(n,U64) for n in ('uploads','prepared','committed','retired','completed_serial')]+[(n,U32) for n in ('free_slots','reserved_slots','submitted_slots')]


def test(args):
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    native=Path(__file__).resolve().parents[1]/'native';exe=out/'uniform_state.dll'
    sources=[native/'d3d9_uniform_state.c',native/'d3d9_uniform_pack.c']
    subprocess.run([*args.cc,'-shared','-O2','-std=c11','-Wall','-Wextra','-Werror','-Wl,--export-all-symbols',*map(str,sources),'-o',str(exe)],check=True)
    lib=C.CDLL(str(exe));functions={}
    signatures={'create':[C.POINTER(Config),C.POINTER(C.c_void_p)],'destroy':[C.c_void_p],
        'bind':[C.c_void_p,U32,C.POINTER(Plan)],'upload':[C.c_void_p,U32,U32,U32,U32,C.POINTER(U32)],
        'prepare':[C.c_void_p,C.POINTER(Draw)],'commit':[C.c_void_p,U64,U64],
        'cancel':[C.c_void_p,U64],'complete':[C.c_void_p,U64],'stats':[C.c_void_p,C.POINTER(Stats)]}
    for name,params in signatures.items():
        fn=getattr(lib,'fnv_uniform_state_'+name);fn.argtypes=params;fn.restype=U32;functions[name]=fn
    create,destroy,bind,upload,prepare,commit,cancel,complete,stats=[functions[n] for n in signatures]
    batch=lib.fnv_uniform_state_commit_batch
    batch.argtypes=[C.c_void_p,C.POINTER(U64),U32,U64];batch.restype=U32
    state=C.c_void_p();assert create(C.byref(Config(256,224,3,64)),C.byref(state))==0
    entries=(Entry*1)(Entry(0,4,1,0,16));plan=Plan(16,1,entries)
    draw=Draw();C.memset(C.byref(draw),0xa5,C.sizeof(draw));before=bytes(draw)
    assert prepare(state,C.byref(draw))==0x8876086c and bytes(draw)==before
    assert bind(state,0,C.byref(plan))==0 and bind(state,1,C.byref(plan))==0
    entries[0].first=999  # Deep copy: changing caller metadata must not change the binding.
    value=(U32*4)(7,8,9,10);assert upload(state,0,0,4,1,value)==1
    assert upload(state,1,0,4,1,value)==1
    assert upload(state,2,0,4,1,value)==0 and upload(state,0,0,255,2,value)==0
    saved=[]
    for i in range(3):
        value[0]=i;assert upload(state,0,0,4,1,value)==1
        d=Draw();assert prepare(state,C.byref(d))==0;saved.append((d,C.string_at(d.data[0],16)))
    assert prepare(state,C.byref(draw))==0x8876021c and bytes(draw)==before
    assert destroy(state)==0x8876021c
    for d,payload in saved:assert C.string_at(d.data[0],16)==payload
    # Model serials only here. The COM/Vulkan test separately observes an actual fence.
    duplicate=(U64*2)(saved[0][0].ticket,saved[0][0].ticket)
    missing=(U64*2)(saved[0][0].ticket,0)
    assert batch(state,duplicate,2,1)==0x8876086c and batch(state,missing,2,1)==0x8876086c
    st=Stats();assert stats(state,C.byref(st))==0 and st.reserved_slots==3 and not st.submitted_slots
    assert commit(state,saved[0][0].ticket,1)==0
    assert commit(state,saved[0][0].ticket,1)==0x8876086c
    assert cancel(state,saved[0][0].ticket)==0x8876086c
    assert commit(state,saved[1][0].ticket,1)==0x8876086c
    assert commit(state,saved[1][0].ticket,2)==0
    assert complete(state,3)==0x8876086c
    assert complete(state,1)==0 and complete(state,1)==0
    assert complete(state,2)==0 and complete(state,1)==0x8876086c
    assert cancel(state,saved[0][0].ticket)==0x8876086c
    assert cancel(state,saved[2][0].ticket)==0
    st=Stats();assert stats(state,C.byref(st))==0 and st.free_slots==3 and st.retired==2
    # Rebinding selects another persistent API register without another upload.
    other_value=(U32*4)(99,98,97,96);assert upload(state,0,0,9,1,other_value)==1
    other=(Entry*1)(Entry(0,9,1,0,16));other_plan=Plan(16,1,other)
    assert bind(state,0,C.byref(other_plan))==0
    bad=(Entry*1)(Entry(0,256,1,0,16));bad_plan=Plan(16,1,bad)
    assert bind(state,0,C.byref(bad_plan))==0x8876086c
    d=Draw();assert prepare(state,C.byref(d))==0 and C.string_at(d.data[0],16)==bytes(other_value)
    assert commit(state,d.ticket,1)==0x8876086c and cancel(state,d.ticket)==0
    assert bind(state,0,None)==0 and prepare(state,C.byref(draw))==0x8876086c
    override=lib.fnv_uniform_state_prepare_vertex
    override.argtypes=[C.c_void_p,C.c_void_p,U32,C.POINTER(Draw)];override.restype=U32
    columns=(U32*16)(*range(16));original=bytes(columns)
    for data,size in ((None,64),(columns,0),(columns,65)):
        assert override(state,data,size,C.byref(draw))==0x8876086c and bytes(draw)==before
    assert override(state,columns,64,C.byref(d))==0
    columns[0]=999
    assert d.bytes[0]==64 and C.string_at(d.data[0],64)==original
    assert C.string_at(d.data[1],16)==bytes((U32*4)(7,8,9,10)) and cancel(state,d.ticket)==0
    # No hidden vertex binding is installed by the override.
    assert prepare(state,C.byref(draw))==0x8876086c and bytes(draw)==before
    assert bind(state,1,None)==0
    assert override(state,columns,64,C.byref(draw))==0x8876086c and bytes(draw)==before
    entries[0].first=4
    assert bind(state,1,C.byref(plan))==0
    zero=Plan(0,0,None);assert bind(state,0,C.byref(zero))==0
    assert prepare(state,C.byref(d))==0 and d.bytes[0]==0 and d.data[0] is None
    assert cancel(state,d.ticket)==0
    entries[0].first=0
    assert bind(state,0,C.byref(plan))==0 and bind(state,1,C.byref(plan))==0
    def writer(stage):
        values=(U32*4)()
        for i in range(400):
            values[:]=[i,i^0xffffffff,i^0x12345678,stage]
            assert upload(state,stage,0,0,1,values)==1
    def reader():
        import struct
        for _ in range(400):
            d=Draw();assert prepare(state,C.byref(d))==0
            for stage in range(2):
                v=struct.unpack('<4I',C.string_at(d.data[stage],16))
                assert v==(0,0,0,0) or v==(v[0],v[0]^0xffffffff,v[0]^0x12345678,stage)
            assert cancel(state,d.ticket)==0
    with ThreadPoolExecutor(max_workers=3) as pool:
        for future in [pool.submit(writer,0),pool.submit(writer,1),pool.submit(reader)]:future.result()
    assert destroy(state)==0
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    report={'status':'PASS','threaded_uploads':800,'threaded_snapshots':400,
        'model_only_serials':True,'gpu_rendered':False,'game_executed':False,
        'executable_sha256':sha(exe),'source_sha256':{str(p):sha(p) for p in [Path(__file__),*sources,
            native/'d3d9_uniform_state.h',native/'d3d9_uniform_pack.h',native/'host_mutex.h']}}
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);test(p.parse_args())

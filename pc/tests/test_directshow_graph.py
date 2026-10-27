"""Exercise translated COM calls and real MP3 pull callbacks; no speakers or game execution."""
import argparse,os,struct
from pathlib import Path
import test_windows_heap as shared
SLOTS=[(0,3),(1,1),(2,1),(3,3),(12,2),(7,1),(8,1),(9,1),(10,3),(7,2),(8,2),(9,2),(10,2),(7,2),(8,3),(9,2),(14,3),(15,2),(16,2),(8,5),(9,3),(12,4)]
def programs(raw):
    raw.extend(bytes(16384-len(raw)));struct.pack_into('<I',raw,0x98+56,0x5000)
    for at in (0x98+224+8,0x98+224+16):struct.pack_into('<I',raw,at,len(raw)-512)
    return {0x402000+i*64:bytes.fromhex('8b4424048b08')+bytes((0xff,0x74,0x24,n*4))*n+bytes((0xff,0x51,slot*4))+b'\xc2'+struct.pack('<H',n*4) for i,(slot,n) in enumerate(SLOTS)}
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output','minimp3','file'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);a=p.parse_args()
    os.environ['FNV_TEST_MP3']=str(a.file.resolve())
    # The codec dependency and private test track are included in the proof.
    pc=Path(__file__).resolve().parents[1]
    extra=[Path(__file__).resolve(),a.file.resolve(),*[a.minimp3.resolve()/n for n in ('minimp3.h','minimp3_ex.h')],
        *[pc/'native'/n for n in ('directshow_graph.h','windows_com_classes.h','windows_com_diag.h','dsound_streams.h','dsound_streams.inc','dsound8.h','dsound_listener.inc','dsound_spatial.inc','pcm_queue.h','mp3_stream.h','pcm_convert.h')]]
    shared.NAMES=['CoInitialize','CoCreateInstance'];shared.ARGS=[1,5];shared.DLLS=['ole32.dll']*2
    a.cc+=['-I'+str(a.minimp3.resolve())]
    shared.test(a,host_source='directshow_graph_host.c',native_sources=('windows_com.c','directshow_graph.c','dsound8.c','pcm_queue.c','mp3_stream.c','pcm_convert.c'),guest_programs=programs,
        extra_inputs=extra,linker_options=('-lole32','-luuid','-lstrmiids','-Wl,--stack,8388608'),strict_modes=())

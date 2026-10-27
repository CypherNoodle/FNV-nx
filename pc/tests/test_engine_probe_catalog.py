"""Original shader envelope/membership preflight with own synthetic tokens."""
from pathlib import Path
import struct
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build_switch_engine_probe import catalog_entries,bundle_membership

vs=struct.pack('<II',0xfffe0200,0xffff);ps=struct.pack('<II',0xffff0200,0xffff)
def record(name,code):return name.encode().ljust(256,b'\0')+struct.pack('<I',len(code))+code
body=record('own vertex',vs)+record('own pixel',ps)
data=struct.pack('<III',100,2,len(body))+body
assert catalog_entries(data)==[vs,ps]
header='static const uint32_t bundle_0_vs_code[]={0xfffe0200u,0xffffu};\nstatic const uint32_t bundle_0_ps_code[]={0xffff0200u,0xffffu};'
assert bundle_membership(header,[vs,ps])==2
invalid=[b'',data[:-1],data+b'X',struct.pack('<III',100,3,len(body))+body,
         struct.pack('<III',99,2,len(body))+body,
         struct.pack('<III',100,0,len(body))+body]
for broken in invalid:
    try:catalog_entries(broken)
    except ValueError:pass
    else:raise AssertionError('Invalid catalog accepted')
for text,codes in [(header,[vs]),('',[vs,ps])]:
    try:bundle_membership(text,codes)
    except ValueError:pass
    else:raise AssertionError('Invalid membership accepted')
print('PASS synthetic catalog extent/count/bytecode envelopes and prepared shader membership')

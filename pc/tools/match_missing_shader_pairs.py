"""Match bounded runtime fingerprints against a private validated SDP catalog.

Outputs indexes and digests only, never shader code. Matches are candidates,
not authorization to link arbitrary SPIR-V stages. Preserve all collisions.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
from build_switch_engine_probe import catalog_entries

ROW=re.compile(r'MissingPair vs=([0-9a-f]{16})/(\d+) ps=([0-9a-f]{16})/(\d+) fixed=(\d+) reason=(\d+) hits=(\d+)')
ATTRS=re.compile(r'MissingPairAttributes vs=([0-9a-f]{16})/(\d+) ps=([0-9a-f]{16})/(\d+) fixed=(\d+) reason=(\d+) state=(\d+) count=(\d+) attrs=([0-9a-f,]*)')

def fingerprint(code):
    value=14695981039346656037
    for byte in code:value=((value^byte)*1099511628211)&0xffffffffffffffff
    return f'{value:016x}'

def match(text,entries):
    index={}
    for position,code in enumerate(entries):
        key=(int.from_bytes(code[:4],'little')>>16,fingerprint(code),len(code))
        index.setdefault(key,[]).append(dict(index=position,sha256=hashlib.sha256(code).hexdigest()))
    latest={}
    # Keep the latest cumulative count, including after a restart, not their sum.
    for found in ROW.finditer(text):
        vs,vb,ps,pb,fixed,reason,hits=found.groups();key=(vs,int(vb),ps,int(pb),int(fixed),int(reason))
        latest[key]=int(hits)
    attributes={}
    for found in ATTRS.finditer(text):
        vs,vb,ps,pb,fixed,reason,state,count,raw=found.groups()
        packed=[int(v,16) for v in raw.split(',') if v]
        if len(packed)!=int(count) or len(packed)>16 or any(v>0xffffff for v in packed):
            raise ValueError('Invalid captured vertex attributes')
        attributes[(vs,int(vb),ps,int(pb),int(fixed),int(reason))]=dict(
            state=int(state),elements=[[v&255,(v>>8)&255,(v>>16)&255] for v in packed])
    result=[]
    for (vs,vb,ps,pb,fixed,reason),hits in latest.items():
        result.append(dict(vertex_hash=vs,vertex_bytes=vb,pixel_hash=ps,pixel_bytes=pb,
            fixed_bytes=fixed,reason=reason,hits=hits,
            attributes=attributes.get((vs,vb,ps,pb,fixed,reason)),
            vertex_candidates=index.get((0xfffe,vs,vb),[]),pixel_candidates=index.get((0xffff,ps,pb),[])))
    return dict(status='MATCHED_DIAGNOSTIC_FINGERPRINTS',pairs=result,linked_programs_created=0)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('log','catalog','output'):parser.add_argument('--'+name,type=Path,required=True)
    args=parser.parse_args()
    report=match(args.log.read_text(errors='replace'),catalog_entries(args.catalog.read_bytes()))
    args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(dict(status=report['status'],pairs=len(report['pairs']))))

"""Synthetic fingerprints; no proprietary shader inputs."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from match_missing_shader_pairs import fingerprint,match

assert fingerprint(b'')=='cbf29ce484222325'
assert fingerprint(b'hello')=='a430d84680aabd0b'
vs=b'\x00\x03\xfe\xff'+b'\xff\xff\x00\x00'
ps=b'\x00\x03\xff\xff'+b'\xff\xff\x00\x00'
row=f'MissingPair vs={fingerprint(vs)}/8 ps={fingerprint(ps)}/8 fixed=0 reason=1 hits='
r=match(row+'2\n'+row+'7\n',[vs,ps,vs])
p=r['pairs'][0]
assert len(r['pairs'])==1 and p['hits']==7 and not r['linked_programs_created']
assert [v['index'] for v in p['vertex_candidates']]==[0,2]
assert [v['index'] for v in p['pixel_candidates']]==[1]
assert not match(row+'1\n',[ps])['pairs'][0]['vertex_candidates']
assert match('no diagnostic rows',[])['pairs']==[]
assert len(match(row+'1\n'+row.replace('reason=1','reason=2')+'3',[])['pairs'])==2
attrs=f'MissingPairAttributes vs={fingerprint(vs)}/8 ps={fingerprint(ps)}/8 fixed=0 reason=1 state=1 count=2 attrs=020000,04010a'
assert match(row+'1\n'+attrs,[vs,ps])['pairs'][0]['attributes']==dict(state=1,elements=[[0,0,2],[10,1,4]])
assert match(row+'1\n'+attrs.replace('state=1','state=3'),[vs,ps])['pairs'][0]['attributes']['state']==3
try:match(row+'1\n'+attrs.replace('count=2','count=3'),[vs,ps])
except ValueError:pass
else:raise AssertionError('Truncated attributes accepted')
print('PASS: vectors, cumulative counters, duplicate candidates, stage filtering, absent identities')

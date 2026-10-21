"""Reject a host-only enlarged budget as fixed-budget Switch evidence."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from startup_memory_profile import configuration,validate_observation,require_switch_profile

checks=0
def reject(call):
    global checks
    try:call()
    except ValueError:checks+=1
    else:raise AssertionError('Invalid diagnostic evidence accepted')

for name in ('bounded64','extended808'):
    c=configuration(name)
    assert c['arena_bytes']-c['vm_bytes']==40<<20
    assert 0<c['commit_limit']<=c['vm_bytes']
    assert all(v%65536==0 for v in c.values())
    host=dict(guest_memory=dict(**c,committed_bytes=4096,reserved_bytes=65536))
    validate_observation(host,name);checks+=1
    for k in c:
        bad=dict(host['guest_memory']);bad[k]+=65536
        reject(lambda:validate_observation(dict(guest_memory=bad),name))
    for n in (-1,c['commit_limit']+4096):
        bad=dict(host['guest_memory']);bad['committed_bytes']=n
        reject(lambda:validate_observation(dict(guest_memory=bad),name))
    if name=='bounded64':require_switch_profile(dict(memory_profile=name,host=host));checks+=1
    else:reject(lambda:require_switch_profile(dict(memory_profile=name,host=host)))
    c['arena_bytes']=0;assert configuration(name)['arena_bytes']>0
reject(lambda:configuration('imaginary'))
reject(lambda:require_switch_profile(dict(memory_profile='imaginary')))
reject(lambda:require_switch_profile(dict(host=dict(worker_stats=dict(capacity=4)))))
require_switch_profile({});checks+=1
print('PASS',checks,'memory profile/accounting and Switch packaging checks')

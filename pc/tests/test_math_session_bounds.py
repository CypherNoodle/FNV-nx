"""Protocol-only bounded-cache tests; the synthetic target does no arithmetic."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from run_target_math_session import session,MAX_SESSION_REQUESTS
from replay_startup_math import decode_cache,MAX_REPLAY_RECORDS
from x87_target_evaluator import sha


class SyntheticTarget:
    def __init__(self,path):
        self.path=path;self.calls=0
        self.report=dict(source_sha256={str(Path(__file__).resolve()):sha(Path(__file__))},
                         generated_sha256={},model='synthetic-protocol-only')

    def evaluate(self,request):
        self.calls+=1
        # Reject unchanged. This deliberately claims no numerical behavior.
        return 1,request[2:]


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    proof=out/'synthetic-target.json';proof.write_text('{"arithmetic":false}\n')
    target=SyntheticTarget(proof);client=out/'client.py'
    client.write_text('''import json,struct,sys
n=int(sys.argv[1]);mode=sys.argv[2]
keys=list(range(n))
if mode=='evict':keys += [0,n-1]
if mode=='hot':keys=list(range(n-1))+[0,n-1,0]
for i in keys:
 request=bytearray(88);struct.pack_into('<I',request,2,i);request[87]=1
 sys.stdout.write('FNV_X87_REQUEST '+request.hex()+'\\n');sys.stdout.flush()
 reply=sys.stdin.buffer.readline()
 if not reply:sys.exit(3)
 row=bytes.fromhex(reply.decode().split()[1])
 assert row==request+struct.pack('<I',1)+request[2:]
print(json.dumps(dict(status='PASS',replies=len(keys))))
''',newline='\n')
    def run(name,n,mode='plain',**kwargs):
        return session([sys.executable,str(client),str(n),mode],target,out/name,timeout=45,**kwargs)
    r=run('eviction',MAX_REPLAY_RECORDS+2,'evict',max_requests=MAX_REPLAY_RECORDS+4)
    assert r['reason']=='CHILD_EXIT' and not r['exit_code'] and r['host']['status']=='PASS'
    assert r['requests']==MAX_REPLAY_RECORDS+4 and r['retained_requests']==MAX_REPLAY_RECORDS
    assert r['evaluations']==MAX_REPLAY_RECORDS+3 and target.calls==r['evaluations']
    assert r['cache_evictions']==3 and r['unique_requests'] is None
    cache=out/'eviction/answers.bin';assert sha(cache)==r['answer_cache_sha256']
    rows=decode_cache(cache.read_bytes());assert len(rows)==MAX_REPLAY_RECORDS
    assert int.from_bytes(rows[-2][0][2:6],'little')==0
    assert int.from_bytes(rows[-1][0][2:6],'little')==MAX_REPLAY_RECORDS+1
    hot=run('hot-key',MAX_REPLAY_RECORDS+1,'hot',max_requests=MAX_REPLAY_RECORDS+3)
    assert hot['requests']==MAX_REPLAY_RECORDS+3 and hot['evaluations']==MAX_REPLAY_RECORDS+1
    assert hot['cache_evictions']==1 and not hot['exit_code']
    hot_rows=decode_cache((out/'hot-key/answers.bin').read_bytes())
    hot_ids={int.from_bytes(row[0][2:6],'little') for row in hot_rows}
    assert 0 in hot_ids and 1 not in hot_ids
    limited=run('default-limit',4097)
    assert limited['requests']==4096 and limited['reason']=='REQUEST_LIMIT' and limited['exit_code']==3
    assert limited['retained_requests']==4096 and limited['cache_evictions']==0 and limited['unique_requests']==4096
    explicit=run('explicit-limit',4,max_requests=3)
    assert explicit['requests']==3 and explicit['reason']=='REQUEST_LIMIT' and explicit['exit_code']==3
    for i,budget in enumerate((0,MAX_SESSION_REQUESTS+1)):
        path=out/('invalid-'+str(i))
        try:session([sys.executable,str(client),'1','plain'],target,path,max_requests=budget)
        except ValueError:pass
        else:raise AssertionError('Invalid budget accepted')
        assert not path.exists()
    report=dict(status='PASS',eviction_replies=r['requests'],retained_records=len(rows),
                evictions=r['cache_evictions'],default_limit_preserved=True,
                explicit_limit=True,hot_key_retained=True,invalid_budgets=2,arithmetic_tested=False,hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))


if __name__=='__main__':main()

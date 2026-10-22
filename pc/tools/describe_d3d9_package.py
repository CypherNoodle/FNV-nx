"""Read-only SDP shader interface inventory; no inferred pairs or vertex formats."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from shader_packages import parse_package
from translate_d3d9_pair import normalize

def describe(package,expected,linker,output):
    sha=lambda b:hashlib.sha256(b).hexdigest()
    data=package.read_bytes()
    if sha(data)!=expected:raise ValueError('package identity mismatch')
    records=parse_package(data);output.mkdir(parents=True,exist_ok=False)
    linker_hash=sha(linker.read_bytes());unique={};rows=[]
    sources=[Path(__file__).resolve(),Path(normalize.__code__.co_filename),Path(parse_package.__code__.co_filename)]
    frozen={str(p):sha(p.read_bytes()) for p in sources}
    env=os.environ.copy();env['PATH']=str(linker.parent)+os.pathsep+env['PATH']
    with tempfile.TemporaryDirectory(prefix='sdp-',dir=output) as temporary:
        for record in records:
            key=record['sha256']
            if key not in unique:
                normalized,centroid=normalize(record['code']);path=Path(temporary)/(key+'.bin');path.write_bytes(normalized)
                p=subprocess.run([str(linker),'--describe',str(path)],capture_output=True,text=True,env=env,timeout=30)
                if p.returncode not in (0,4):raise RuntimeError('unexpected translator failure: '+str(p.returncode))
                result=dict(accepted=p.returncode==0,centroid_inputs=len(centroid),normalized_sha256=sha(normalized))
                if not p.returncode:
                    metadata=json.loads(p.stdout)
                    if metadata['stage']!={'vertex':2,'pixel':1}[record['stage']]:raise ValueError('translator stage mismatch')
                    result['metadata']=metadata
                else:result['errors']=p.stderr.replace(str(path),'<private-bytecode>')
                unique[key]=result
            rows.append(dict(index=record['index'],sha256=key,stage=record['stage'],major=record['major'],minor=record['minor']))
    if sha(package.read_bytes())!=expected or sha(linker.read_bytes())!=linker_hash:raise ValueError('input changed')
    if any(sha(Path(p).read_bytes())!=h for p,h in frozen.items()):raise ValueError('tool source changed')
    report=dict(status='COMPLETE',package_sha256=expected,linker_sha256=linker_hash,
                records=rows,unique=unique,accepted=sum(r['accepted'] for r in unique.values()),
                rejected=sum(not r['accepted'] for r in unique.values()),
                pairs_inferred=False,vertex_formats_inferred=False,centroid_reapplied=False,
                gpu_rendered=False,game_started=False,source_sha256=frozen)
    (output/'interfaces.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('package','linker','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--sha256',required=True);a=p.parse_args()
    r=describe(a.package.resolve(),a.sha256,a.linker.resolve(),a.output.resolve())
    print(json.dumps({k:r[k] for k in ('status','accepted','rejected','pairs_inferred','gpu_rendered')}))

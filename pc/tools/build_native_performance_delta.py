"""Compile own optimized native units against a retained baseline object set.

This is explicit object replacement, not a fresh historical source audit. Input
and output receipts are private. Never replace a generated game function here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

UNITS=('d3d9_vk_backend.c','d3d9_com.c','dsound8.c','audio_horizon.c','windows_vorbis.c','bink_decoder.c','bink_movie.c')
CHANGED_BASELINE_DEPENDENCIES=('d3d9_vk_backend.h','d3d9_vk_draw.inc','d3d9_vk_textures.inc','dsound8.h','media_horizon.h','windows_vorbis.h','bink_decoder.h','bink_movie.h')

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def verify(proof_path,baseline_path):
    proof_path=Path(proof_path).resolve();baseline_path=Path(baseline_path).resolve()
    r=json.loads(proof_path.read_text());baseline=json.loads(baseline_path.read_text())
    pc=Path(__file__).resolve().parents[1]
    if r.get('status')!='NATIVE_PERFORMANCE_DELTA_COMPILED' or r['baseline_sha256']!=sha(baseline_path):raise ValueError('Wrong native baseline')
    if r['compiler_flags']!=baseline['compiler_flags'] or r['sdk_sha256']!=baseline['sdk_sha256']:raise ValueError('Changed native ABI/flags')
    if {Path(row['source']).name for row in r['compiled']}!=set(UNITS) or len(r['compiled'])!=len(UNITS):raise ValueError('Unexpected native unit')
    required={str((pc/'native'/n).resolve()) for n in (*UNITS,'resource_lookup.h','audio_phase.h',*CHANGED_BASELINE_DEPENDENCIES)}
    required.add(str(Path(__file__).resolve()))
    for pattern in ('*.h','*.inc'):
        required.update(str(p.resolve()) for p in (pc/'native').glob(pattern))
    for row in baseline['compiled']:
        if Path(row['source']).name not in UNITS:continue
        required.add(str(Path(row['command'][0]).resolve()))
        for item in row['command']:
            if item.startswith('-I'):required.update(str(p.resolve()) for p in Path(item[2:]).rglob('*.h'))
    if not required.issubset(r['source_sha256']):raise ValueError('Missing native dependencies')
    for name,digest in r['source_sha256'].items():
        if sha(name)!=digest:raise ValueError('Changed native input: '+name)
    replacements={}
    for row in r['compiled']:
        source=Path(row['source']).resolve()
        if source!=pc/'native'/source.name:raise ValueError('Native source outside own tree')
        candidates=[v for v in baseline['compiled'] if Path(v['source']).resolve()==source]
        if len(candidates)!=1:raise ValueError('Native source lacks baseline')
        old=candidates[0];expected=list(old['command']);expected[expected.index('-o')+1]=row['object']
        if row['command']!=expected or row['original_sha256']!=old['sha256'] or sha(old['object'])!=old['sha256']:raise ValueError('Native command/object provenance mismatch')
        if sha(row['object'])!=row['sha256']:raise ValueError('Changed optimized native object')
        replacements[str(Path(old['object']).resolve())]=Path(row['object']).resolve()
    return r,replacements,{str((pc/'native'/n).resolve()) for n in CHANGED_BASELINE_DEPENDENCIES}

def run(a):
    pc=Path(__file__).resolve().parents[1];baseline=json.loads(a.baseline.read_text())
    if baseline.get('status')!='PASS':raise ValueError('Invalid baseline runtime')
    a.output.mkdir(parents=True,exist_ok=False)
    rows=[r for r in baseline['compiled'] if Path(r['source']).name in UNITS]
    if len(rows)!=len(UNITS):raise ValueError('Missing baseline native units')
    frozen={str(Path(__file__).resolve()):sha(Path(__file__))}
    for pattern in ('*.h','*.inc'):
        for path in (pc/'native').glob(pattern):frozen[str(path.resolve())]=sha(path)
    result=[]
    for row in rows:
        src=Path(row['source']);frozen[str(src.resolve())]=sha(src)
        if sha(row['object'])!=row['sha256']:raise ValueError('Changed original object')
        command=list(row['command']);target=(a.output/(src.stem+'.o')).resolve()
        compiler=Path(command[0]);frozen[str(compiler.resolve())]=sha(compiler)
        # All headers visible through the original include roots are frozen.
        for item in command:
            if item.startswith('-I'):
                for path in Path(item[2:]).rglob('*.h'):frozen[str(path.resolve())]=sha(path)
        command[command.index('-o')+1]=str(target)
        r=subprocess.run(command,capture_output=True,text=True)
        (a.output/(src.stem+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError('Native compile failed: '+src.name)
        result.append(dict(source=str(src.resolve()),object=str(target),sha256=sha(target),
                           original_sha256=row['sha256'],command=command))
    for name,digest in frozen.items():
        if sha(name)!=digest:raise ValueError('Native input changed while compiling')
    report=dict(status='NATIVE_PERFORMANCE_DELTA_COMPILED',baseline_sha256=sha(a.baseline),
                compiler_flags=baseline['compiler_flags'],sdk_sha256=baseline['sdk_sha256'],
                compiled=result,source_sha256=frozen,fresh_historical_source_audit=False)
    proof=a.output/'validation.json';proof.write_text(json.dumps(report,indent=2)+'\n')
    verify(proof,a.baseline);print(json.dumps(dict(status=report['status'],units=list(UNITS))))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);run(p.parse_args())

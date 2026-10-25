"""Build pinned external Xiph sources for native callback decoding; no playback."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile

PINNED={
    'ogg':('e1774cd77f471443541596e09078e78fdc342e4f','588af45a3329b89b4506e5205fd1c99e57f088f492b2a247fe9472f42ffe8e5f'),
    'vorbis':('0657aee69dec8508a0011f47f3b69d7538e9d262','6ccc3fa4cfd3072f9a772025cc8fd712098d081c00171b09b6627ffced7faa88')}
VORBIS='mdct smallft block envelope window lsp lpc analysis synthesis psy info floor1 floor0 res0 mapping0 registry codebook sharedbook lookup bitrate vorbisfile vorbisenc'.split()

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def build(a):
    out=a.output.resolve()
    if out.exists():raise ValueError('Use fresh output')
    inputs={n:getattr(a,n+'_archive').resolve() for n in PINNED}
    for n,p in inputs.items():
        if sha(p)!=PINNED[n][1]:raise ValueError('Pinned archive mismatch: '+n)
    out.mkdir(parents=True);roots={}
    for n,p in inputs.items():
        prefix=n+'-'+PINNED[n][0]
        with tarfile.open(p,'r:gz') as archive:
            members=archive.getmembers()
            if sum(m.size for m in members)>128*1024*1024:raise ValueError('Expanded budget exceeded')
            for m in members:
                if not (m.name==prefix or m.name.startswith(prefix+'/')) or '..' in Path(m.name).parts or m.issym() or m.islnk() or not (m.isfile() or m.isdir()):raise ValueError('Unsafe archive entry')
            archive.extractall(out,filter='data')
        roots[n]=out/prefix
    inc=out/'include/ogg';inc.mkdir(parents=True)
    (inc/'config_types.h').write_text('#ifndef FNV_OGG_TYPES_H\n#define FNV_OGG_TYPES_H\n#include <stdint.h>\n'+''.join('typedef '+t+' ogg_'+name+';\n' for t,name in [('int16_t','int16_t'),('uint16_t','uint16_t'),('int32_t','int32_t'),('uint32_t','uint32_t'),('int64_t','int64_t'),('uint64_t','uint64_t')])+'#endif\n',newline='\n')
    includes=[out/'include',roots['ogg']/'include',roots['vorbis']/'include',roots['vorbis']/'lib']
    flags=['-std=gnu11','-O2','-ffp-contract=off','-fno-strict-aliasing','-Werror=implicit-function-declaration']
    if a.switch:
        from build_cpu_runtime import SWITCH_FLAGS
        flags=[*SWITCH_FLAGS,'-O2','-DHAVE_ALLOCA_H']
    cc=a.cc.resolve();ar=a.ar.resolve();env=os.environ.copy();env['PATH']=str(cc.parent)+os.pathsep+env.get('PATH','')
    sources=[roots['ogg']/'src'/f'{n}.c' for n in ('framing','bitwise')]+[roots['vorbis']/'lib'/f'{n}.c' for n in VORBIS]
    frozen={str(p):sha(p) for p in [*inputs.values(),cc,ar,Path(__file__).resolve(),*[p for root in roots.values() for p in root.rglob('*') if p.is_file()]]}
    rows=[]
    for i,p in enumerate(sources):
        obj=out/f'codec_{i:02d}.o';cmd=[str(cc),*flags,*['-I'+str(p) for p in includes],'-c',str(p),'-o',str(obj)]
        r=subprocess.run(cmd,capture_output=True,text=True,env=env,timeout=120);(out/f'compile_{i:02d}.log').write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError('Compile failed: '+str(p))
        rows.append(dict(source=str(p),object=str(obj),sha256=sha(obj),command=cmd))
    library=out/'libfnv_vorbis.a';r=subprocess.run([str(ar),'rcs',str(library),*[r['object'] for r in rows]],capture_output=True,text=True,env=env,timeout=120)
    (out/'archive.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError('Archiving failed')
    if not all(sha(Path(p))==h for p,h in frozen.items()):raise ValueError('Input changed')
    report=dict(status='COMPILED',switch_abi=a.switch,library=str(library),library_sha256=sha(library),includes=list(map(str,includes)),objects=rows,source_sha256=frozen,pinned=PINNED,audio_played=False,game_started=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print('COMPILED Xiph Vorbis',len(rows),'objects; Switch ABI:',a.switch)
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('output','ogg-archive','vorbis-archive','cc','ar'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--switch',action='store_true');build(p.parse_args())

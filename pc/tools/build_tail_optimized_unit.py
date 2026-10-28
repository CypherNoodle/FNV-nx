"""Recompile one unchanged private unit and verify selected tail-cycle edges.

The sole extra flag is -foptimize-sibling-calls. Both legacy CALL26 controls and
replacement JUMP26 edges are required. No game bodies are embedded in this tool.
"""
import argparse,hashlib,json,re,subprocess,shutil
from pathlib import Path
from run_aot_analysis import sha,sdk_identity
def run(a):
    if a.output.exists():raise ValueError('Use a fresh directory')
    gen=json.loads(a.generated_validation.read_text());runtime=json.loads(a.runtime_validation.read_text());source=a.source.resolve();sdk=a.sdk.resolve()
    rows=[r for r in gen['compiled'] if Path(r['source']).resolve()==source]
    if len(rows)!=1 or gen['status']!='COMPILED' or runtime['status']!='PASS' or gen['compiler_flags']!=runtime['compiler_flags']:raise ValueError('Generation/runtime disagreement')
    row=rows[0];old=Path(row['object']);digest=sha(source)
    if digest!=gen['source_sha256'][str(source)] or sha(old)!=row['sha256']:raise ValueError('Changed baseline')
    sdk_hash=sdk_identity(sdk)
    if sdk_hash!=runtime['sdk_sha256'] or runtime['source_sha256'].get(str(a.cc.resolve()))!=sha(a.cc):raise ValueError('Changed compiler/SDK')
    edges=[];text=source.read_text()
    for value in a.edge:
        parts=value.split(':')
        if len(parts)!=2 or any(not re.fullmatch('sub_[0-9A-F]{8}',p) for p in parts):raise ValueError('Invalid explicit edge')
        if 'void '+parts[0]+'(void)' not in text or not re.search(r'\b'+parts[1]+r'\(\);\s*return;',text):raise ValueError('Missing tail source')
        edges.append(parts)
    if len(edges)<2 or len({tuple(e) for e in edges})!=len(edges):raise ValueError('Require distinct cycle edges')
    frozen={str(p.resolve()):sha(p) for p in (a.generated_validation,a.runtime_validation,source,old,a.cc,a.objdump,Path(__file__))}
    for p in [*source.parent.glob('*.h'),*Path(__file__).resolve().parents[1].joinpath('native').glob('*.h')]:frozen[str(p.resolve())]=sha(p)
    a.output.mkdir(parents=True);out=a.output.resolve();copy=out/source.name;shutil.copyfile(source,copy);obj=copy.with_suffix('.o')
    pc=Path(__file__).resolve().parents[1]
    cmd=[str(a.cc.resolve()),*gen['compiler_flags'],'-foptimize-sibling-calls',*['-I'+str(p) for p in (pc/'native',source.parent,sdk/'templates/runtime',sdk/'src/kernel',a.libnx.resolve()/'include')],'-include',str(pc/'native/fnv_sdk_runtime.h'),'-c',str(copy),'-o',str(obj)]
    p=subprocess.run(cmd,capture_output=True,text=True);(out/'compiler.log').write_text(p.stdout+p.stderr)
    if p.returncode:raise RuntimeError(p.stderr[-2000:])
    dumps=[]
    for name,o in [('legacy',old),('optimized',obj)]:
        p=subprocess.run([str(a.objdump.resolve()),'-dr',str(o)],capture_output=True,text=True,check=True);(out/(name+'.asm')).write_text(p.stdout);dumps.append(p.stdout)
    for src,dest in edges:
        for dump,kind,forbidden in [(dumps[0],'CALL26','JUMP26'),(dumps[1],'JUMP26','CALL26')]:
            start=dump.index('<'+src+'>:');end=dump.find('\nDisassembly of section',start);body=dump[start:end if end>=0 else None]
            if not re.search(r'R_AARCH64_'+kind+r'\s+'+dest+r'\b',body) or re.search(r'R_AARCH64_'+forbidden+r'\s+'+dest+r'\b',body):raise ValueError('Unexpected machine edge '+src+':'+dest)
    if sdk_identity(sdk)!=sdk_hash or any(sha(Path(p))!=h for p,h in frozen.items()):raise ValueError('Inputs changed')
    result=dict(status='TAIL_OPTIMIZATION_COMPILED',original_unit=str(source),original_sha256=digest,corrected_source=str(copy),corrected_sha256=sha(copy),object=str(obj),object_sha256=sha(obj),compiler_flags=gen['compiler_flags'],optimizer_flags=['-foptimize-sibling-calls'],command=cmd,sdk_sha256=sdk_hash,source_sha256=frozen,verified_edges=edges,legacy_edges='CALL26',optimized_edges='JUMP26',source_unchanged=True,switch_hardware_tested=False)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(result['status'],len(edges),'machine edges')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('generated-validation','runtime-validation','source','sdk','libnx','cc','objdump','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--edge',action='append',required=True);run(p.parse_args())

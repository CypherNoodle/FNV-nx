"""Check LF policy before byte-hashed tests/publication; --fix is explicit.

Uses this repository's Git index, or a JSON path manifest for source overlays.
Never follows an input path outside root or normalizes SDK/generated binaries.
"""
import argparse
import json
from pathlib import Path
import subprocess


def check(root, names, fix=False):
    root=root.resolve();pending=[];checked=0
    for name in dict.fromkeys(names):
        relative=Path(name)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('Require repository-relative paths')
        if any(p in ('third_party','local','dist') for p in relative.parts):continue
        if not (relative.parts and relative.parts[0]=='pc' or relative.as_posix() in ('README.md','.gitattributes')):
            continue
        path=(root/relative).resolve()
        if not path.is_relative_to(root):raise ValueError('Path leaves repository')
        data=path.read_bytes();normalized=data.replace(b'\r\n',b'\n')
        normalized.decode('utf-8')
        if b'\0' in data or b'\r' in normalized:raise ValueError('Non-text or bare CR: '+name)
        checked+=1
        if data!=normalized:pending.append((path,normalized))
    if fix:
        for path,data in pending:path.write_bytes(data)
    return dict(checked=checked,crlf_files=[str(p.relative_to(root)) for p,_ in pending],fixed=bool(fix))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2])
    p.add_argument('--manifest',type=Path,help='JSON array of paths, or objects with a path field')
    p.add_argument('--fix',action='store_true')
    a=p.parse_args()
    if a.manifest:
        rows=json.loads(a.manifest.read_text());names=[r if isinstance(r,str) else r['path'] for r in rows]
    else:
        top=subprocess.check_output(['git','rev-parse','--show-toplevel'],cwd=a.root,text=True).strip()
        if Path(top).resolve()!=a.root.resolve():raise ValueError('Root is not this Git checkout; provide a manifest')
        names=subprocess.check_output(['git','ls-files','-z','--','pc','README.md','.gitattributes'],cwd=a.root).decode().split('\0')
        names=[n for n in names if n]
    result=check(a.root,names,a.fix);print(json.dumps(result,indent=2))
    if result['crlf_files'] and not a.fix:raise SystemExit(1)

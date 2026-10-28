"""Revalidate an unchanged Mesa archive against a retained complete static ELF.

This recovers a platform input proof without inventing a deleted renderer-build
report. It does not compile or execute a renderer and does not approve shaders.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1<<20),b''):h.update(block)
    return h.hexdigest()


def validate(a):
    out=a.output.resolve()
    if out.exists():raise ValueError('Use a fresh output directory')
    proof=json.loads(a.platform_validation.read_text())
    if proof.get('status')!='FINAL_LINKED' or proof.get('unresolved_strong_symbols')!=[] or proof.get('dynamic_library_dependencies')!=[]:
        raise ValueError('Require a complete statically linked platform proof')
    candidates=[(Path(p).resolve(),h) for p,h in proof['source_sha256'].items() if Path(p).name=='libvulkan.a']
    archive=a.archive.resolve();elf=Path(proof['elf']).resolve()
    if len(candidates)!=1 or candidates[0][0]!=archive or sha(archive)!=candidates[0][1]:
        raise ValueError('Archive differs from retained static platform link')
    if sha(elf)!=proof['elf_sha256']:raise ValueError('Retained ELF changed')
    sources={str(p):sha(p) for p in (a.platform_validation.resolve(),archive,elf,a.nm.resolve(),a.readelf.resolve(),Path(__file__).resolve())}
    if proof['source_sha256'].get(str(a.nm.resolve()))!=sha(a.nm) or proof['source_sha256'].get(str(a.readelf.resolve()))!=sha(a.readelf):
        raise ValueError('ELF inspection tools differ from retained link')
    def inspect(args):return subprocess.check_output(list(map(str,args)),text=True)
    defined=inspect([a.nm,'--defined-only','--extern-only','--format=posix',elf])
    if not any(len(p:=line.split())>=2 and p[0]=='vkGetInstanceProcAddr' and p[1]=='T' for line in defined.splitlines()):
        raise ValueError('Static Vulkan entry point missing from retained ELF')
    undefined=inspect([a.nm,'--undefined-only','--format=posix',elf])
    if any(len(p:=line.split())>=2 and p[1]=='U' for line in undefined.splitlines()):raise ValueError('Unresolved strong symbols')
    headers=inspect([a.readelf,'-h','-l','-d',elf])
    if 'AArch64' not in headers or 'DYN' not in headers or '(NEEDED)' in headers:raise ValueError('Invalid static Horizon ELF')
    if any(sha(p)!=h for p,h in sources.items()):raise ValueError('Inputs changed during inspection')
    result=dict(status='PASS',static_vulkan=True,historical_static_link_revalidated=True,
                fresh_renderer_build=False,game_started=False,switch_hardware_tested=False,
                source_sha256=sources,limits=['Unchanged archive identity and retained static link only; no renderer execution'])
    out.mkdir(parents=True);(out/'elf-headers.log').write_text(headers);(out/'undefined.log').write_text(undefined)
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='source_sha256'}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('platform-validation','archive','nm','readelf','output'):p.add_argument('--'+key,type=Path,required=True)
    validate(p.parse_args())

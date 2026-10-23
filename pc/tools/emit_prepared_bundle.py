"""Emit one PRIVATE build header for explicit offline-linked program fixtures.

The manifest lists fixture paths, numeric input maps and sampler masks. Relative
paths resolve against it. Original and derived shader data must never be published.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
from emit_prepared_program import render

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def emit_bundle(manifest,output):
    if output.exists():raise FileExistsError(output)
    if manifest.stat().st_size>1024*1024:raise ValueError('manifest too large')
    manifest_hash=sha(manifest);request=json.loads(manifest.read_text())
    if not isinstance(request,dict) or set(request)!={'schema','programs'} or type(request['schema']) is not int:raise ValueError('invalid bundle manifest')
    rows=request['programs']
    if request['schema']!=1 or not isinstance(rows,list) or not 0<len(rows)<=4096:raise ValueError('invalid bundle manifest')
    lines=['/* PRIVATE generated shader bundle; do not publish. */','#include "d3d9_vk_prepared.h"']
    frozen={str(manifest.resolve()):manifest_hash};pairs={};data_symbols={};program_symbols=[];cached={};source_bytes=0;duplicates=0
    declaration=re.compile(r'^(static const (?:uint32_t|struct fnv_uniform_entry|struct fnv_vk_vertex_input|struct fnv_vk_prepared_program) )([A-Za-z_][A-Za-z_0-9]*)(.*)$')
    for row in rows:
        if not isinstance(row,dict) or set(row)!={'fixture','inputs','sampler_mask'} or not isinstance(row['fixture'],str):raise ValueError('invalid program row')
        fixture=(manifest.parent/row['fixture']).resolve();mapping=row['inputs'];mask=row['sampler_mask']
        if not isinstance(mapping,list) or any(not isinstance(i,list) or any(type(v) is not int for v in i) for i in mapping) or type(mask) is not int:raise ValueError('invalid program map/mask')
        key=(str(fixture),json.dumps(mapping),mask)
        if key not in cached:
            text,proof=render(fixture,mapping,mask);cached[key]=(text,proof)
        else:text,proof=cached[key]
        for p,h in proof['input_sha256'].items():
            if p in frozen and frozen[p]!=h:raise ValueError('input changed between programs')
            frozen[p]=h
        report=json.loads((fixture/'pair/translation.json').read_text())
        pair=tuple(report['stages'][s]['original_sha256'] for s in ('vs','ps'))
        signature=json.dumps({'stages':[{k:report['stages'][s][k] for k in ('original_sha256','uniform_plan')}|{'spirv':report['stages'][s]['spirv']['sha256']} for s in ('vs','ps')],'inputs':mapping,'mask':mask},sort_keys=True)
        if pair in pairs:
            if pairs[pair]!=signature:raise ValueError('conflicting payload/map/mask for the same original pair')
            duplicates+=1;continue
        pairs[pair]=signature;aliases={};index=len(program_symbols)
        for line in text.splitlines():
            if line.startswith('/*') or line.startswith('#include'):continue
            match=declaration.fullmatch(line)
            if not match:raise ValueError('unexpected generated declaration')
            prefix,name,body=match.groups()
            body=re.sub(r'\b[A-Za-z_][A-Za-z_0-9]*\b',lambda m:aliases.get(m[0],m[0]),body)
            data_key=(prefix,body)
            if 'struct fnv_vk_prepared_program ' not in prefix and data_key in data_symbols:
                aliases[name]=data_symbols[data_key];continue
            symbol='bundle_%d_%s'%(index,name);aliases[name]=symbol
            emitted=prefix+symbol+body;source_bytes+=len(emitted)+1
            if source_bytes>32*1024*1024:raise ValueError('bundle source budget exceeded')
            lines.append(emitted)
            if 'struct fnv_vk_prepared_program ' not in prefix:data_symbols[data_key]=symbol
        program_symbols.append(aliases['fnv_startup_program'])
    lines+=['#define FNV_STARTUP_PROGRAM_COUNT %d'%len(program_symbols),
            'static const struct fnv_vk_prepared_program *const fnv_startup_programs[]={'+','.join('&'+s for s in program_symbols)+'};']
    if any(sha(Path(p))!=h for p,h in frozen.items()):raise ValueError('bundle inputs changed')
    text='\n'.join(lines)+'\n';output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('x',encoding='ascii',newline='\n') as f:f.write(text)
    return dict(status='PASS',schema='fnv-prepared-bundle-v1',requested_programs=len(rows),programs=len(program_symbols),duplicate_programs=duplicates,shared_data_definitions=len(data_symbols),input_sha256=frozen,tool_sha256=sha(Path(__file__)),emitter_sha256=sha(Path(render.__code__.co_filename)),header_sha256=sha(output),private_generated_output=True,semantic_validation=False,engine_started=False)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manifest',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();print(json.dumps(emit_bundle(a.manifest.resolve(),a.output.resolve()),indent=2))

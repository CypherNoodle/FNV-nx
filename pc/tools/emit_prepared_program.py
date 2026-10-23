"""Emit a PRIVATE build header for one exact offline-linked shader pair.

Contains user shader bytecode and SPIR-V: never publish generated output.
Requires explicit numeric usage/index/location mappings and sampler mask.
No inferred declarations or runtime shader pairing; no semantic validation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from inspect_spirv import inspect
from spirv_uniform_layout import reflect

def render(fixture,inputs,sampler_mask):
    if not inputs or len(inputs)>32 or any(len(i)!=3 for i in inputs):raise ValueError('invalid input mappings')
    if len({tuple(i[:2]) for i in inputs})!=len(inputs) or len({i[2] for i in inputs})!=len(inputs):raise ValueError('invalid input mappings')
    if any(len(i)!=3 or not 0<=i[0]<=13 or not 0<=i[1]<=15 or not 0<=i[2]<32 for i in inputs) or sampler_mask&~65535:raise ValueError('unsupported mapping/mask')
    pair=fixture/'pair';report=json.loads((pair/'translation.json').read_text())
    if report['status']!='PASS':raise ValueError('translation not accepted')
    digest=lambda b:hashlib.sha256(b).hexdigest();header=['/* PRIVATE generated user shader data; do not publish. */','#include "d3d9_vk_prepared.h"'];stages=[];frozen={str((pair/'translation.json').resolve()):digest((pair/'translation.json').read_bytes())}
    for stage in ('vs','ps'):
        source=fixture/(stage+'.bin');spv=pair/(stage+'.spv');code=source.read_bytes();binary=spv.read_bytes();r=report['stages'][stage]
        if digest(code)!=r['original_sha256'] or digest(binary)!=r['spirv']['sha256']:raise ValueError('shader identity mismatch')
        reflected=inspect(binary)
        if reflected['entry']['model']!=(0 if stage=='vs' else 4):raise ValueError('stage mismatch')
        if stage=='vs' and {v['location'] for v in reflected['interfaces'] if v['storage']==1 and 'location' in v}!={i[2] for i in inputs}:raise ValueError('vertex locations mismatch')
        metadata=report['link']['vertex_uniform_metadata' if stage=='vs' else 'pixel_uniform_metadata']
        if reflect(binary,metadata)!=r['uniform_plan']:raise ValueError('uniform plan mismatch')
        samplers={x['binding'] for x in reflected['resources'] if x['storage']==0}
        if stage=='vs' and samplers or stage=='ps' and samplers!={i for i in range(16) if sampler_mask&(1<<i)}:raise ValueError('sampler mask mismatch')
        for resource in reflected['resources']:
            if resource['storage']==0 and (stage!='ps' or resource['set']!=2):raise ValueError('unsupported sampler descriptor')
            if resource['storage']==2 and (resource['set']!=(1 if stage=='vs' else 3) or resource['binding']!=0):raise ValueError('unsupported uniform descriptor')
        for path,data,name in ((source,code,stage+'_code'),(spv,binary,stage+'_spv')):
            if len(data)%4:raise ValueError('unaligned data')
            header.append('static const uint32_t '+name+'[]={'+','.join('0x%08xu'%w for w in struct.unpack('<%dI'%(len(data)//4),data))+'};')
            frozen[str(path.resolve())]=digest(data)
        plan=r['uniform_plan'];entries=plan['entries'];pointer='NULL'
        if entries:
            pointer=stage+'_entries';header.append('static const struct fnv_uniform_entry '+pointer+'[]={'+','.join('{'+','.join(str(e[k]) for k in ('kind','first','count','offset','stride'))+'}' for e in entries)+'};')
        stages.append('{'+stage+'_code,'+stage+'_spv,sizeof('+stage+'_code),sizeof('+stage+'_spv),{'+str(plan['bytes'])+','+str(len(entries))+','+pointer+'}}')
    header.append('static const struct fnv_vk_vertex_input startup_inputs[]={'+','.join('{'+','.join(map(str,i))+'}' for i in inputs)+'};')
    header.append('static const struct fnv_vk_prepared_program fnv_startup_program={{'+','.join(stages)+'},startup_inputs,'+str(len(inputs))+','+str(sampler_mask)+'};')
    if any(hashlib.sha256(Path(p).read_bytes()).hexdigest()!=h for p,h in frozen.items()):raise ValueError('input changed')
    text='\n'.join(header)+'\n'
    return text,dict(status='PASS',input_sha256=frozen,header_sha256=digest(text.encode('ascii')),tool_sha256=digest(Path(__file__).read_bytes()),private_generated_output=True,semantic_validation=False,game_started=False)

def emit(fixture,inputs,sampler_mask,output):
    text,report=render(fixture,inputs,sampler_mask)
    output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('x',encoding='ascii',newline='\n') as f:f.write(text)
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--input',action='append',required=True,help='usage,index,location');p.add_argument('--sampler-mask',type=lambda s:int(s,0),required=True)
    a=p.parse_args();r=emit(a.fixture.resolve(),[list(map(int,i.split(','))) for i in a.input],a.sampler_mask,a.output.resolve());print(json.dumps(r))

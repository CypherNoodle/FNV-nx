"""Prepare PRIVATE Vulkan programs from observed bytecode/declaration triples.

No shader bytes or generated headers belong in the public repository. Successful
offline linking is not semantic validation or evidence of a rendered game frame.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import struct
from types import SimpleNamespace
from translate_d3d9_pair import digest, normalize, translate
from emit_prepared_program import render

# D3DDECLTYPE -> external MojoShader vertex element format. Unsupported packed
# formats have no guessed substitute. The runtime still owns actual byte fetches.
FORMATS={0:0,1:1,2:2,3:3,4:4,5:5,6:6,7:7,9:8,10:9,15:10,16:11}


def declaration(data):
    if not data or len(data)%8 or len(data)>65*8:raise ValueError('invalid declaration size')
    result={}
    for at in range(0,len(data),8):
        stream,offset,kind,method,usage,index=struct.unpack_from('<HHBBBB',data,at)
        if stream==255:
            if data[at:]!=b'\xff\0\0\0\x11\0\0\0' or not result:raise ValueError('invalid declaration terminator')
            return result
        if stream>15 or method or usage>13 or index>15 or kind not in FORMATS:
            raise ValueError('unsupported declaration element')
        if (usage,index) in result:raise ValueError('duplicate declaration semantic')
        result[usage,index]=FORMATS[kind]
    raise ValueError('missing declaration terminator')


def vertex_semantics(code):
    normalized,_=normalize(code);words=struct.unpack('<%dI'%(len(normalized)//4),normalized)
    major=(words[0]>>8)&255
    if words[0]>>16!=0xfffe or major not in (1,2,3):raise ValueError('require explicit vertex declarations')
    if major==1 and words[0]!=0xfffe0101:raise ValueError('unsupported legacy vertex version')
    result={};seen=set();pos=1;ended=False
    while pos<len(words):
        token=words[pos];op=token&65535
        if op==65535:
            if token!=65535 or pos!=len(words)-1:raise ValueError('invalid END or trailing bytecode')
            ended=True;break
        if op==65534:length=(token>>16)&32767
        elif major==1:
            # Bounded legacy family with explicit DCL: NOP, MOV, ADD, MAD, DEF.
            sizes={0:0,1:2,2:3,4:4,31:2,81:5}
            if token!=op or op not in sizes:raise ValueError('unsupported VS1.1 instruction shape')
            length=sizes[op]
        else:length=(token>>24)&15
        if pos+1+length>len(words):raise ValueError('truncated vertex instruction')
        if op==31:
            if length!=2:raise ValueError('invalid vertex DCL')
            semantic,dest=words[pos+1:pos+3];kind=((dest>>28)&7)|((dest>>8)&24)
            if kind==1:
                register=dest&2047;key=(semantic&15,(semantic>>16)&15)
                if key[0]>13 or register in result or key in seen:raise ValueError('duplicate or unsupported vertex input')
                result[register]=key;seen.add(key)
        pos+=length+1
    if not ended or not result:raise ValueError('missing END or explicit vertex inputs')
    return result


def input_mappings(reflection,semantics):
    result=[];registers=set()
    for item in reflection['interfaces']:
        if item['storage']!=1:continue
        match=re.fullmatch(r'vs_v(\d+)',item.get('name',''))
        if not match or 'location' not in item:raise ValueError('unrecognized vertex interface')
        register=int(match[1])
        if register not in semantics or register in registers:raise ValueError('unmapped vertex register')
        registers.add(register);result.append([*semantics[register],item['location']])
    if not result or len({r[2] for r in result})!=len(result):raise ValueError('invalid reflected locations')
    return result


def prepare(capture,output,linker,build):
    capture=capture.resolve();output=output.resolve();linker=linker.resolve();build=build.resolve()
    if output.exists():raise FileExistsError('Use a fresh private output directory')
    proof=json.loads(build.read_text())
    if proof['executable_sha256']!=digest(linker.read_bytes()):raise ValueError('linker identity mismatch')
    if proof['adapter_sha256']!=digest(Path(__file__).with_name('link_d3d9_spirv.c').read_bytes()):raise ValueError('different offline adapter')
    pairs=sorted(capture.glob('world-pair-*-vs.bin'))
    if not 0<len(pairs)<=64:raise ValueError('require 1..64 captured pairs')
    frozen={str(linker):digest(linker.read_bytes()),str(build):digest(build.read_bytes())}
    for name in ('prepare_observed_shader_pairs.py','translate_d3d9_pair.py','emit_prepared_program.py','inspect_spirv.py','spirv_uniform_layout.py'):
        source=Path(__file__).with_name(name).resolve();frozen[str(source)]=digest(source.read_bytes())
    records=[];manifest=[]
    output.mkdir(parents=True)
    for vs in pairs:
        match=re.fullmatch(r'world-pair-(\d{2})-vs.bin',vs.name)
        if not match:raise ValueError('invalid capture filename')
        number=match[1];ps=capture/f'world-pair-{number}-ps.bin';decl=capture/f'world-pair-{number}-decl.bin'
        for p in (vs,ps,decl):frozen[str(p)]=digest(p.read_bytes())
        fixture=output/f'pair-{number}';fixture.mkdir()
        record={'pair':number,'status':'REJECTED'}
        try:
            elements=declaration(decl.read_bytes());semantics=vertex_semantics(vs.read_bytes())
            if not set(semantics.values())<=elements.keys():raise ValueError('vertex input absent from actual declaration')
            attributes=fixture/'attributes.txt';attributes.write_text(''.join(f'{u} {i} {fmt}\n' for (u,i),fmt in elements.items()),encoding='ascii')
            shutil.copyfile(vs,fixture/'vs.bin');shutil.copyfile(ps,fixture/'ps.bin')
            report=translate(SimpleNamespace(vs=fixture/'vs.bin',ps=fixture/'ps.bin',attributes=attributes,linker=linker,output=fixture/'pair'))
            mappings=input_mappings(report['stages']['vs']['spirv'],semantics);mask=0
            for resource in report['stages']['ps']['spirv']['resources']:
                if resource['storage']==0:
                    if resource['set']!=2 or not 0<=resource['binding']<16:raise ValueError('unsupported pixel descriptor')
                    mask|=1<<resource['binding']
            text,meta=render(fixture,mappings,mask);(fixture/'program.h').write_text(text,encoding='ascii',newline='\n')
            manifest.append({'fixture':fixture.name,'inputs':mappings,'sampler_mask':mask})
            record.update(status='PREPARED',inputs=mappings,sampler_mask=mask,header_sha256=digest(text.encode('ascii')))
        except (ValueError,RuntimeError) as error:record['reason']=str(error)
        records.append(record)
    if any(digest(Path(p).read_bytes())!=h for p,h in frozen.items()):raise ValueError('capture or linker changed during preparation')
    (output/'manifest.json').write_text(json.dumps({'schema':1,'programs':manifest},indent=2)+'\n')
    result=dict(status='PASS' if len(manifest)==len(pairs) else 'PARTIAL',observed_pairs=len(pairs),prepared_pairs=len(manifest),records=records,input_sha256=frozen,private_generated_output=True,semantic_validation=False,engine_started=False)
    (output/'validation.json').write_text(json.dumps(result,indent=2)+'\n');return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('capture','output','linker','linker-build'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();r=prepare(a.capture,a.output,a.linker,a.linker_build)
    print(json.dumps({k:r[k] for k in ('status','observed_pairs','prepared_pairs')}))

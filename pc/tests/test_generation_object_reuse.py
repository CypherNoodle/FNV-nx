"""Cache eligibility must follow exact C, compiler and header dependencies."""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from generation_object_reuse import candidates
from run_aot_analysis import generation_paths,sha


def test(out):
    if out.exists():raise ValueError('Fresh private output required')
    out=out.resolve();out.mkdir(parents=True);analysis=out/'analysis';analysis.mkdir()
    old=generation_paths(analysis,'old');new=generation_paths(analysis,'new');old['generated'].mkdir();new['generated'].mkdir()
    common=out/'common.h';common.write_text('#define ABI 1\n');compiler=out/'compiler.bin';compiler.write_bytes(b'fixture compiler')
    files={'recomp_0000.c':'void first(void) {}\n','recomp_0001.c':'void second(void) {}\n','recomp_funcs.h':'void first(void); void second(void);\n'}
    for folder in (old['generated'],new['generated']):
        for name,text in files.items():(folder/name).write_text(text)
    report={'generation_name':'old','output_files':{n:sha(old['generated']/n) for n in files}}
    old['report'].write_text(json.dumps(report));prior={'generation_report_sha256':sha(old['report']),
        'source_sha256':{str(p):sha(p) for p in [common,compiler,old['report'],*[old['generated']/n for n in files]]}}
    current={'output_files':{n:sha(new['generated']/n) for n in files}};frozen={p:sha(p) for p in [common,*[new['generated']/n for n in files]]}
    sources=[new['generated']/n for n in files if n.endswith('.c')];checks=0
    core=None
    def call():return candidates(out/'prior.json',prior,current,new,frozen,sources,compiler,core)
    def reject():
        nonlocal checks
        try:call()
        except ValueError:checks+=1
        else:raise AssertionError('Incompatible cache accepted')
    assert call()[0]=={'recomp_0000.c','recomp_0001.c'};checks+=1
    # Core objects are rebuilt separately. Reusing generated C only permits
    # relocation of the identical generated platform header, never ABI changes.
    old_core=out/'old-core';new_core=out/'new-core';old_core.mkdir();new_core.mkdir()
    for folder in (old_core,new_core):
        header=folder/'platform.h';header.write_text('#define LITTLEENDIAN 1\n')
        proof=dict(status='COMPILED',switch_abi=True,transcendental_model='fixture',source_sha256={str(header):sha(header)})
        (folder/'validation.json').write_text(json.dumps(proof))
    old_proof=old_core/'validation.json';core=new_core/'validation.json'
    prior['x87_core_validation_sha256']=sha(old_proof)
    prior['source_sha256'].update({str(old_proof):sha(old_proof),str(old_core/'platform.h'):sha(old_core/'platform.h')})
    frozen.update({core:sha(core),new_core/'platform.h':sha(new_core/'platform.h')})
    assert call()[0]=={'recomp_0000.c','recomp_0001.c'};checks+=1
    header=new_core/'platform.h';header.write_text('#define LITTLEENDIAN 0\n');reject();header.write_text('#define LITTLEENDIAN 1\n')
    saved=core.read_text();info=json.loads(saved);info['transcendental_model']='changed';core.write_text(json.dumps(info));frozen[core]=sha(core);reject()
    core.write_text(saved);frozen[core]=sha(core)
    missing=prior['source_sha256'].pop(str(old_proof));reject();prior['source_sha256'][str(old_proof)]=missing
    prior['x87_core_validation_sha256']='0'*64;reject();prior['x87_core_validation_sha256']=sha(old_proof)
    assert call()[0]=={'recomp_0000.c','recomp_0001.c'};checks+=1
    sources[1].write_text('void second(void) { (void)0; }\n');assert call()[0]=={'recomp_0000.c'};checks+=1
    sources[1].write_text(files['recomp_0001.c'])
    compiler.write_bytes(b'changed compiler');reject();compiler.write_bytes(b'fixture compiler')
    common.write_text('#define ABI 2\n');reject();common.write_text('#define ABI 1\n')
    frozen[common]='0'*64;reject();frozen[common]=sha(common)
    current['output_files']['recomp_funcs.h']='0'*64;reject();current['output_files']['recomp_funcs.h']=sha(new['generated']/'recomp_funcs.h')
    archived=old['generated']/'recomp_funcs.h';archived.write_text('void changed(void);');reject();archived.write_text(files[archived.name])
    archived=old['generated']/'recomp_0000.c';archived.write_text('void changed(void) {}');reject();archived.write_text(files[archived.name])
    original=old['report'].read_text();old['report'].write_text('{}');reject();old['report'].write_text(original)
    frozen[old['generated']/'unexpected.h']='0'*64;reject();del frozen[old['generated']/'unexpected.h']
    assert call()[0]=={'recomp_0000.c','recomp_0001.c'};checks+=1
    paths=[Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'tools/generation_object_reuse.py']
    result=dict(status='PASS',checks=checks,objects_executed=False,source_sha256={str(p):sha(p) for p in paths})
    (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS',checks,'source/compiler/header cache checks')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);test(p.parse_args().output)

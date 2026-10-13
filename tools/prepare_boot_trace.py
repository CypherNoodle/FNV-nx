"""Create private diagnostic TUs, preserving canonical generated output.

Only adds scope observers: no original instructions, branches or registers
are replaced. The diagnostic copies must never be published with the glue.
"""
from pathlib import Path
import hashlib,json,re,argparse
here=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument("--generated-dir",type=Path,default=here/"generated/default")
args=parser.parse_args()
reports=here/"local/reports";reports.mkdir(parents=True,exist_ok=True)
out=here/'build/boot-trace';out.mkdir(parents=True,exist_ok=True)
targets={'105':['8260E250'],'129':['8260DFB8'],'12':['82556DB0'],
         '228':['82616D38'],'172':['82219460'],'25':['82557648']}
rows=[]
for partition,functions in targets.items():
    source=args.generated_dir/f'fnv_xbox_recomp.{partition}.cpp'
    original=source.read_text()
    text='#include "host_boot_trace.h"\n'+original
    for address in functions:
        pattern=f'DEFINE_REX_FUNC(sub_{address}) {{\n\tREX_FUNC_PROLOGUE();'
        assert text.count(pattern)==1
        text=text.replace(pattern,pattern+f'\n\tFnvBootTraceScope fnv_trace(0x{address}, ctx, base);')
    if partition=='228':
        start=text.index('DEFINE_REX_FUNC(sub_82616D38)')
        end=text.index('\nDEFINE_REX_FUNC(',start+1)
        body=text[start:end]
        # Observe the last original call reached if bootstrap stalls. The
        # complete original call remains immediately after the observation.
        body=re.sub(r'(?m)^(\t)(sub_([0-9A-F]{8})\(ctx, base\);)$',
                    lambda m:f'\tFnvObserveBootScope(0x82616D38,"call_{m[3]}",ctx,base);\n'+m[0],body)
        text=text[:start]+body+text[end:]
    restored=text.removeprefix('#include "host_boot_trace.h"\n')
    restored=re.sub(r'(?m)^\tFnvBootTraceScope fnv_trace\([^\n]+\);\n','',restored)
    restored=re.sub(r'(?m)^\tFnvObserveBootScope\([^\n]+\);\n','',restored)
    assert restored==original,'Diagnostic copy changed canonical instructions'
    dest=out/source.name
    if not dest.exists() or dest.read_text()!=text:dest.write_text(text)
    rows.append({'source':str(source),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
                 'copy':str(dest),'copy_sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),
                 'functions':functions,'canonical_body_roundtrip_verified':True})
(reports/'boot-trace-copies.json').write_text(json.dumps(rows,indent=2)+'\n')
print('Prepared',len(rows),'private diagnostic TUs')

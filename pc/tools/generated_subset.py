"""Read an identity-checked generation for private diagnostic subsets.

Uses an existing complete code/data proof; never repairs functions or removes
runtime traps. Bodies, references and reports contain game code: keep private.
"""
import json
from pathlib import Path
import re
from aot_table_selection import verify_derivation
from build_cpu_runtime import validate_selection_proof
from run_aot_analysis import generation_paths,sha
from verify_aot_code_data import mask_noncode,dispatch


def read_generation(analysis,name,proof_path,decoder,sdk_sha256):
    paths=generation_paths(analysis,name);report=json.loads(paths['report'].read_text())
    proof=json.loads(proof_path.read_text());selection=verify_derivation(analysis,decoder)
    validate_selection_proof(proof,selection,report,sha(paths['report']))
    if not report['full_selection'] or report['gaps']['failed_functions'] or report['sdk_sha256']!=sdk_sha256:
        raise ValueError('Require a complete generation with this SDK and no failed functions')
    if proof.get('source_sha256'):
        for source,digest in proof['source_sha256'].items():
            if sha(Path(source))!=digest:raise ValueError('Changed generation proof input: '+source)
    else:raise ValueError('Require frozen proof dependencies for generated-code experiments')
    inputs=[paths['report'],proof_path,*[Path(p) for p in proof['source_sha256']]]
    table=dispatch(paths['generated']/'recomp_dispatch.c');by_symbol={symbol:at for at,symbol in table.items()}
    if len(by_symbol)!=len(table):raise ValueError('Aliased dispatch symbols require explicit support')
    named=[symbol for symbol in by_symbol if not re.fullmatch(r'sub_[0-9A-Fa-f]{8}',symbol)]
    reference_pattern=re.compile(r'\b(?:sub_[0-9A-Fa-f]{8}'+''.join('|'+re.escape(symbol) for symbol in named)+r')\b')
    functions={};references={};braces=re.compile(r'[{}]')
    for name,digest in report['output_files'].items():
        if Path(name).name!=name:raise ValueError('Invalid generated filename')
        path=paths['generated']/name
        if sha(path)!=digest:raise ValueError('Changed generated input: '+name)
        inputs.append(path)
        if not re.fullmatch(r'recomp_\d{4}\.c',name):continue
        text=path.read_text();clean=mask_noncode(text)
        for match in re.finditer(r'^void (\w+)\(void\)\s*\{',clean,re.M):
            level=1;close=None
            for brace in braces.finditer(clean,match.end()):
                level+=1 if brace[0]=='{' else -1
                if not level:close=brace.end();break
            if match[1] not in by_symbol:raise ValueError('Generated definition absent from dispatch')
            at=by_symbol[match[1]]
            if close is None or at in functions:raise ValueError('Malformed or duplicate generated function')
            functions[at]=text[match.start():close]+'\n'
            references[at]={by_symbol[v] if v in by_symbol else int(v[4:],16) for v in reference_pattern.findall(clean[match.end():close])}
    if len(functions)!=report['functions_generated'] or set(functions)!=set(table):
        raise ValueError('Generated bodies do not match the complete function dispatch')
    return functions,references,table,inputs,dict(generation_report_sha256=sha(paths['report']),
        generation_proof_sha256=sha(proof_path),functions_available=len(functions),
        remaining_unresolved_targets=report['gaps']['unresolved_call_targets'])

"""Select unchanged generated C objects after a fresh complete-generation audit."""
import json
from pathlib import Path
from run_aot_analysis import generation_paths,sha


def candidates(prior_path,prior,report,paths,frozen,sources,compiler,core_path=None):
    reports=[Path(p) for p,h in prior['source_sha256'].items() if h==prior['generation_report_sha256'] and Path(p).name.startswith('generation-report')]
    if len(reports)!=1:raise ValueError('Prior generation report identity unavailable')
    old_path=reports[0]
    if sha(old_path)!=prior['generation_report_sha256']:raise ValueError('Changed prior generation report')
    old_report=json.loads(old_path.read_text());old_generated=generation_paths(old_path.parent,old_report['generation_name'])['generated']
    if prior['source_sha256'].get(str(compiler.resolve()))!=sha(compiler.resolve()):raise ValueError('Changed compiler')
    old_headers={Path(p).resolve():h for p,h in prior['source_sha256'].items() if Path(p).suffix=='.h' and Path(p).resolve().parent!=old_generated.resolve()}
    new_headers={p.resolve():h for p,h in frozen.items() if p.suffix=='.h' and p.resolve().parent!=paths['generated'].resolve()}
    dependencies={old_path:sha(old_path)}
    if prior.get('x87_core_validation_sha256') or core_path:
        # A freshly rebuilt arithmetic core gets a new output directory. Only
        # its generated platform.h may move, and only with identical bytes.
        # All guest/runtime/SDK headers retain their exact paths and hashes.
        matches=[Path(p).resolve() for p,h in prior['source_sha256'].items()
                 if h==prior.get('x87_core_validation_sha256') and Path(p).suffix=='.json']
        if not core_path or len(matches)!=1:raise ValueError('Missing exact prior/current arithmetic core proof')
        old_core=matches[0];new_core=core_path.resolve()
        if sha(old_core)!=prior['x87_core_validation_sha256'] or frozen.get(new_core)!=sha(new_core):
            raise ValueError('Changed arithmetic core proof')
        old_info=json.loads(old_core.read_text());new_info=json.loads(new_core.read_text())
        if any(c.get('status')!='COMPILED' or c.get('switch_abi') is not True for c in (old_info,new_info)) or old_info.get('transcendental_model')!=new_info.get('transcendental_model'):
            raise ValueError('Changed arithmetic core mode')
        old_platform=old_core.parent/'platform.h';new_platform=new_core.parent/'platform.h'
        h=old_headers.pop(old_platform,None);fresh=new_headers.pop(new_platform,None)
        if not h or h!=fresh or sha(old_platform)!=h or sha(new_platform)!=h:
            raise ValueError('Changed generated core platform header')
        if old_info['source_sha256'].get(str(old_platform))!=h or new_info['source_sha256'].get(str(new_platform))!=h:
            raise ValueError('Platform header absent from exact core inventories')
        dependencies.update({old_core:sha(old_core),new_core:sha(new_core),old_platform:h,new_platform:h})
    if old_headers!=new_headers:raise ValueError('Changed compiled header dependencies')
    old_gen_headers={n:h for n,h in old_report['output_files'].items() if n.endswith('.h')}
    new_gen_headers={n:h for n,h in report['output_files'].items() if n.endswith('.h')}
    if old_gen_headers!=new_gen_headers:raise ValueError('Changed generated ABI headers')
    for p,h in old_headers.items():
        if sha(p)!=h:raise ValueError('Changed prior header: '+str(p))
        dependencies[p]=h
    for n,h in old_gen_headers.items():
        p=old_generated/n
        if sha(p)!=h:raise ValueError('Changed prior generated header')
        dependencies[p]=h
    eligible=set()
    for source in sources:
        digest=old_report['output_files'].get(source.name)
        if digest!=sha(source):continue
        old_source=(old_generated/source.name).resolve()
        if prior['source_sha256'].get(str(old_source))!=digest or sha(old_source)!=digest:raise ValueError('Changed archived source for reused object')
        eligible.add(source.name);dependencies[old_source]=digest
    return eligible,dependencies

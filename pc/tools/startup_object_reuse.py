"""Reuse identical diagnostic objects; never reuse an old arithmetic core."""
import json
from pathlib import Path
from run_aot_analysis import sha


def reusable(prior_path,compiler,flags,sources,generated,output,frozen,header_roots):
    prior_path=prior_path.resolve();prior=json.loads(prior_path.read_text());base=prior_path.parent
    if (prior.get('status') not in ('EXPERIMENT_STOPPED','EXPERIMENT_RETURNED') or
        not prior.get('all_generated') or prior.get('compiler_flags')!=flags or not prior.get('compiled')):
        raise ValueError('Incompatible prior complete host diagnostic')
    policy=prior.get('host_x87') or {};digest=policy.get('core_validation_sha256')
    matches=[Path(p).resolve() for p,h in prior['source_sha256'].items() if h==digest and Path(p).suffix=='.json']
    if policy.get('schema')!='fnv-startup-host-x87-v1' or len(matches)!=1:
        raise ValueError('Host reuse requires an exact extended arithmetic/driver identity')
    core=matches[0]
    if sha(core)!=digest or json.loads(core.read_text()).get('compiler')!=compiler:
        raise ValueError('Changed prior core or compiler invocation')
    binary=Path(compiler[0]).resolve()
    if prior['source_sha256'].get(str(binary))!=sha(binary):raise ValueError('Changed compiler binary')
    def shared_headers(items):
        return {Path(p).resolve():h for p,h in items.items()
                if Path(p).suffix in ('.h','.inc') and any(Path(p).resolve().is_relative_to(root.resolve()) for root in header_roots)}
    old_headers=shared_headers(prior['source_sha256']);new_headers=shared_headers(frozen)
    if old_headers!=new_headers or any(sha(p)!=h for p,h in old_headers.items()):
        raise ValueError('Changed diagnostic ABI/SDK headers')
    old_generated=prior['generated_files_sha256']
    old_h={n:h for n,h in old_generated.items() if n.endswith('.h')}
    new_h={n:h for n,h in generated.items() if n.endswith('.h')}
    if old_h!=new_h:raise ValueError('Changed generated diagnostic headers')
    for n,h in old_h.items():
        if Path(n).name!=n or sha(base/n)!=h or sha(output/n)!=h:raise ValueError('Changed archived/current diagnostic header')
        frozen[str(base/n)]=h
    inventory={item['source']:item for item in prior['compiled']}
    if len(inventory)!=len(prior['compiled']):raise ValueError('Duplicate old compiler unit')
    result={}
    if len({s.name for s in sources})!=len(sources):raise ValueError('Duplicate current compiler unit')
    for source in sources:
        item=inventory.get(source.name)
        if item is None:continue
        expected=old_generated.get(source.name,prior['source_sha256'].get(str(source.resolve())))
        if expected!=sha(source):continue
        if source.name in old_generated:
            archived=base/source.name
            if sha(archived)!=expected:raise ValueError('Changed archived generated source')
            frozen[str(archived)]=expected
        if Path(item['object']).name!=item['object']:raise ValueError('Unsafe cached object filename')
        obj=base/item['object']
        if sha(obj)!=item['object_sha256']:raise ValueError('Changed cached diagnostic object')
        frozen[str(obj)]=item['object_sha256'];result[source.name]=obj
    frozen[str(prior_path)]=sha(prior_path);frozen[str(core)]=digest
    frozen[str(Path(__file__).resolve())]=sha(Path(__file__).resolve())
    return result

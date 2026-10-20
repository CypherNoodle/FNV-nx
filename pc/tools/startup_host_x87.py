"""Exact host arithmetic core selection for a deliberately bounded CRT probe.

Target-only math must stop explicitly; this is not a substitute host model.
"""
import json
from pathlib import Path
from run_aot_analysis import sha
from x87_models import MODELS,model_flags


def inputs(report,core_path,compiler,stop_target_math):
    info=report.get('x87_extended_overlay')
    if not info:
        if core_path or stop_target_math:raise ValueError('Legacy generation cannot select extended host diagnostics')
        return [],{},[],None
    if not core_path:raise ValueError('Extended startup requires a verified host arithmetic core')
    if info.get('legacy_runtime_references'):raise ValueError('Extended startup still contains legacy FP state')
    profile=info['profile'];model=profile.get('transcendental_model')
    if model is not None and model not in MODELS:raise ValueError('Unknown target numerical model')
    if bool(model)!=bool(stop_target_math):raise ValueError('Target math requires its explicit diagnostic-stop mode')
    core=json.loads(core_path.read_text())
    if (core.get('status')!='COMPILED' or core.get('switch_abi') is not False or
        core.get('transcendental_model') is not None or
        core['compiler'][1:]!=compiler[1:] or Path(core['compiler'][0]).resolve()!=Path(compiler[0]).resolve()):
        raise ValueError('Require matching native-host strict arithmetic core/compiler')
    frozen={Path(k).resolve():h for k,h in info['source_sha256'].items()}
    for name,h in core['source_sha256'].items():
        p=Path(name).resolve()
        if p in frozen and frozen[p]!=h:raise ValueError('Host core/generation source mismatch')
        frozen[p]=h
    objects=[];sources=set()
    for item in core['objects']:
        p=Path(item['object']).resolve();source=Path(item['source']).name
        if p in objects or source in sources:raise ValueError('Duplicate host arithmetic object')
        objects.append(p);sources.add(source);frozen[p]=item['sha256']
    if len(objects)!=99 or not {'x87_stack.c','x87_extended.c','softfloat_state.c','extF80_add.c'}<=sources:
        raise ValueError('Incomplete or non-strict host core')
    frozen[core_path.resolve()]=sha(core_path);frozen[Path(__file__).resolve()]=sha(Path(__file__).resolve())
    if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Changed host core or generation input')
    flags=['-DFNV_CPU_EXTENDED_X87=1',*model_flags(model)]
    policy=dict(schema='fnv-startup-host-x87-v1',profile=profile,core_validation_sha256=sha(core_path),
                arithmetic_objects=len(objects),target_math_behavior='explicit-diagnostic-stop' if model else 'not-selected',
                target_numerical_model_executed=False)
    return flags,frozen,objects,policy


def validate_policy(desktop,report):
    """Validate the host observation's limited numerical scope before packaging."""
    info=report.get('x87_extended_overlay');policy=desktop.get('host_x87')
    objects=desktop.get('host_x87_objects',[])
    if not info:
        if policy or objects:raise ValueError('Legacy target has extended host evidence')
        return
    model=info['profile'].get('transcendental_model')
    if (not isinstance(policy,dict) or policy.get('schema')!='fnv-startup-host-x87-v1' or
        policy.get('profile')!=info['profile'] or policy.get('arithmetic_objects')!=99 or
        policy.get('target_numerical_model_executed') is not False or
        policy.get('target_math_behavior')!=('explicit-diagnostic-stop' if model else 'not-selected')):
        raise ValueError('Missing/mismatched extended host diagnostic policy')
    flags=desktop.get('compiler_flags',[])
    if not all(flag in flags for flag in ['-DFNV_CPU_EXTENDED_X87=1',*model_flags(model)]):
        raise ValueError('Host observation lacks the target CPU build mode')
    if len(objects)!=99 or len({o['object'] for o in objects})!=99:
        raise ValueError('Host observation has an incomplete arithmetic core')
    frozen=desktop.get('source_sha256',{})
    if any(frozen.get(str(Path(o['object']).resolve()))!=o['sha256'] for o in objects):
        raise ValueError('Host arithmetic objects are not frozen in the observation')

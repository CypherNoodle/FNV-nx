"""Rebuild the complete private x86 C generation with the Switch CPU ABI.

Requires a successful code/data audit, an external SDK and devkitA64. Produces
private objects and a relocatable link, never an NRO or evidence of game boot.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
from aot_code_data import verify_derivation
from run_aot_analysis import generation_paths, sdk_identity, sha

SWITCH_FLAGS = [
    '-std=gnu11', '-O1', '-fno-strict-aliasing', '-ffp-contract=off',
    '-mearly-ra=none', '-mno-outline-atomics', '-march=armv8-a+crc+crypto',
    '-mtune=cortex-a57', '-mtp=soft', '-ftls-model=local-exec', '-fPIE',
    '-ffunction-sections', '-fdata-sections', '-D__SWITCH__',
    '-Werror=implicit-function-declaration',
]
PLATFORM_SYMBOLS = {
    '__aarch64_read_tp', '__getreent', 'abort', 'atan2', 'calloc', 'cos',
    'exp2', 'fclose', 'fmod', 'fopen', 'fprintf', 'fread', 'free', 'frexp',
    'fseek', 'ftell', 'ldexp', 'log', 'longjmp',
    'memcpy', 'memset', 'remainder', 'setjmp', 'sin', 'snprintf', 'sqrt',
    'sqrtf', 'strcmp', 'strstr', 'tan',
}


def run(command, log):
    result = subprocess.run([str(s) for s in command], capture_output=True, text=True)
    log.write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f'Command failed ({result.returncode}); see {log}')
    return result.stdout


def validate_selection_proof(audit, selection, report, report_sha256):
    structure_only = audit.get('status') == 'STRUCTURE_PASS'
    if (audit.get('status') not in ('PASS','STRUCTURE_PASS') or
            (structure_only and (audit.get('audit_kind') != 'table-selection-structure-v1' or
                                 audit.get('objects_compiled') is not False or audit.get('compiled') != [] or
                                 not audit.get('source_sha256'))) or
            audit.get('retained_function_bodies_identical') is not True or
            audit.get('variant_report_sha256') != report_sha256 or
            audit.get('selection') != selection or report.get('code_data_filter') != selection):
        raise ValueError('Missing or mismatched prior code/data validation')
    return structure_only


def x87_build_inputs(report, profile_path, core_path, compiler):
    """Fail closed before compiling an opt-in extended generation.

    Returns exact flags, frozen dependencies and separately compiled core
    objects. Legacy generations cannot consume an extended core implicitly.
    """
    info=report.get('x87_extended_overlay')
    if not info:
        if profile_path or core_path:raise ValueError('Legacy generation cannot select an extended x87 core')
        return list(SWITCH_FLAGS),{},[],None
    if not profile_path or not core_path:raise ValueError('Extended x87 generation requires its explicit profile and Switch core proof')
    if info.get('legacy_runtime_references'):raise ValueError('Extended generation still uses legacy FP globals; migrate those paths before compiling')
    from aot_x87_codegen import load_profile
    if load_profile(profile_path)!=info['profile'] or sha(profile_path)!=info['profile_sha256'] or info.get('cpu_compile_define')!='FNV_CPU_EXTENDED_X87=1':
        raise ValueError('Extended generation/profile mismatch')
    frozen={Path(path).resolve():digest for path,digest in info['source_sha256'].items()}
    for path,digest in frozen.items():
        if sha(path)!=digest:raise ValueError('Changed extended generation input: '+str(path))
    core=json.loads(core_path.read_text())
    if (core.get('status')!='COMPILED' or core.get('switch_abi') is not True or
        core.get('flags',[])[:len(SWITCH_FLAGS)]!=SWITCH_FLAGS or
        len(core.get('compiler',[]))!=1 or Path(core['compiler'][0]).resolve()!=compiler.resolve()):
        raise ValueError('Require matching actual Switch ABI x87 core compilation')
    sources={Path(o['source']).name for o in core.get('objects',[])}
    if not {'x87_stack.c','x87_extended.c','extF80_add.c','softfloat_state.c'}<=sources:
        raise ValueError('Incomplete extended core object inventory')
    for path,digest in core['source_sha256'].items():
        q=Path(path).resolve()
        if sha(q)!=digest:raise ValueError('Changed x87 core source: '+str(q))
        if q in frozen and frozen[q]!=digest:raise ValueError('Generation/core source disagreement')
        frozen[q]=digest
    objects=[]
    for item in core['objects']:
        q=Path(item['object']).resolve()
        if q in objects or sha(q)!=item['sha256']:raise ValueError('Changed or duplicate x87 core object')
        objects.append(q);frozen[q]=item['sha256']
    frozen[profile_path.resolve()]=sha(profile_path);frozen[core_path.resolve()]=sha(core_path)
    helper=Path(__file__).with_name('aot_x87_codegen.py').resolve();frozen[helper]=sha(helper)
    return [*SWITCH_FLAGS,'-DFNV_CPU_EXTENDED_X87=1'],frozen,objects,sha(core_path)


def build(a):
    if not 1 <= a.jobs <= 8:
        raise ValueError('Use 1..8 compile jobs')
    if a.output.exists():
        raise ValueError('Use a fresh private output directory')
    pc = Path(__file__).resolve().parents[1]
    sdk = a.sdk.resolve()
    analysis = a.analysis.resolve()
    paths = generation_paths(analysis, a.generation_name)
    sys.path.insert(0, str(a.python_deps.resolve()))
    import capstone
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    selection = verify_derivation(analysis, decoder)
    report = json.loads(paths['report'].read_text())
    compile_flags,x87_dependencies,x87_objects,x87_proof=x87_build_inputs(report,getattr(a,'x87_profile',None),getattr(a,'x87_core_validation',None),a.cc)
    if x87_objects and a.reuse_generated_validation:raise ValueError('Extended CPU ABI requires a fresh build; legacy object reuse is disabled')
    audit = json.loads(a.audit_validation.read_text())
    if not selection or not report['full_selection'] or report['gaps']['failed_functions']:
        raise ValueError('Require a complete, successfully generated derived selection')
    structure_only = validate_selection_proof(audit,selection,report,sha(paths['report']))
    if structure_only:
        # This build compiles every source below with the Switch ABI. A structural
        # proof is not accepted as evidence of any pre-existing compiled object.
        for name,digest in audit['source_sha256'].items():
            if sha(Path(name)) != digest:raise ValueError('Changed structural audit input: '+name)
    sdk_hash = sdk_identity(sdk)
    if sdk_hash != report['sdk_sha256']:
        raise ValueError('SDK identity changed')
    frozen = dict(x87_dependencies)
    if structure_only:frozen.update({Path(name):digest for name,digest in audit['source_sha256'].items()})
    for name, digest in report['output_files'].items():
        path = paths['generated'] / name
        if sha(path) != digest:
            raise ValueError('Modified generated input: ' + name)
        frozen[path] = digest
    for field, stem in (('codegen_overlay', 'simd_bits'), ('multiply_overlay', 'mul_flags')):
        for key, path in (('source_sha256', pc / 'tools' / ('aot_' + stem + '.py')),
                          ('runtime_header_sha256', pc / 'native' / ('fnv_' + stem + '.h'))):
            if sha(path) != report[field][key]:
                raise ValueError('Changed generation overlay/runtime: ' + str(path))
            frozen[path] = sha(path)
    if report.get('frame_restore_overlay'):
        path=pc/'tools/aot_frame_restore.py'
        if sha(path)!=report['frame_restore_overlay']['source_sha256']:
            raise ValueError('Changed frame restore overlay')
        frozen[path]=sha(path)
    if report.get('normal_frame_helpers'):
        if not report.get('frame_restore_overlay'):raise ValueError('Helper ABI requires frame restore overlay')
        from audit_seh_helpers import verify
        verify(analysis,report['normal_frame_helpers'])
        path=pc/'tools/audit_seh_helpers.py';frozen[path]=sha(path)
    if report.get('eh3_frame_overlay'):
        from audit_eh3_helpers import verify as verify_eh3
        info=report['eh3_frame_overlay'];verify_eh3(analysis,info['audit'])
        path=pc/'tools/aot_eh3_frames.py'
        if sha(path)!=info['source_sha256']:raise ValueError('Changed EH3 overlay')
        frozen[path]=sha(path);path=pc/'tools/audit_eh3_helpers.py';frozen[path]=sha(path)
    for path in [pc / 'native' / n for n in (
            'cpu_runtime.c', 'cpu_runtime.h', 'fnv_sdk_runtime.h', 'pe_image.c', 'pe_image.h')]:
        frozen[path] = sha(path)
    for directory in (sdk / 'templates/runtime', sdk / 'src/kernel'):
        for path in directory.rglob('*.h'):
            frozen[path] = sha(path)
    for path in (Path(__file__).resolve(), paths['report'], a.audit_validation.resolve(),
                 a.cc.resolve(), a.ld.resolve(), a.nm.resolve(), a.objdump.resolve()):
        frozen[path] = sha(path)
    output = a.output.resolve()
    output.mkdir(parents=True)
    sources = [paths['generated'] / n for n in sorted(report['output_files']) if n.endswith('.c')]
    sources += [pc / 'native/cpu_runtime.c', pc / 'native/pe_image.c']
    if len({p.stem for p in sources}) != len(sources):
        raise ValueError('Object names would collide')
    includes = [pc / 'native', sdk / 'templates/runtime', sdk / 'src/kernel', paths['generated']]
    reused={}
    if a.reuse_generated_validation:
        prior_path=a.reuse_generated_validation.resolve();prior=json.loads(prior_path.read_text())
        if (prior['status']!='PASS' or prior['compiler_flags']!=SWITCH_FLAGS or
            prior['sdk_sha256']!=sdk_hash or
            prior['compile_units']!=len(sources) or len(prior['compiled'])!=len(sources)):
            raise ValueError('Incompatible prior generated-object validation')
        # Only the native CPU/PE implementation and this validator may differ.
        # Generated C uses fnv_sdk_runtime/SDK, not cpu_runtime.h or pe_image.h.
        allowed_changes={Path(__file__).resolve(),*[pc/'native'/n for n in ('cpu_runtime.c','cpu_runtime.h','pe_image.c','pe_image.h')]}
        same_generation=prior['generation_report_sha256']==sha(paths['report'])
        if same_generation:
            for name,digest in prior['source_sha256'].items():
                path=Path(name).resolve()
                if path not in allowed_changes and sha(path)!=digest:raise ValueError('Changed reused dependency: '+name)
            for path in (p for p in frozen if p.suffix in ('.c','.h') and p not in allowed_changes):
                if re.search(r'#\s*include\s*[<"][^>"\n]*(?:cpu_runtime|pe_image)\.h[>"]',path.read_text()):
                    raise ValueError('Generated code includes changed native ABI header')
            eligible={p.name for p in sources[:-2]}
        else:
            from generation_object_reuse import candidates
            eligible,dependencies=candidates(prior_path,prior,report,paths,frozen,sources[:-2],a.cc)
            frozen.update(dependencies)
            helper=pc/'tools/generation_object_reuse.py';frozen[helper]=sha(helper)
        old={item['source']:item for item in prior['compiled']}
        if set(old)!={p.name for p in sources}:raise ValueError('Prior compiled inventory mismatch')
        for source in sources[:-2]:
            if source.name not in eligible:continue
            item=old[source.name];obj=prior_path.parent/(source.stem+'.o')
            if item['exit_code'] or obj.stat().st_size!=item['object_bytes'] or sha(obj)!=item['object_sha256']:
                raise ValueError('Changed prior generated object: '+source.name)
            frozen[obj]=sha(obj);reused[source.name]=obj
        frozen[prior_path]=sha(prior_path)
    def compile_one(src):
        obj = output / (src.stem + '.o')
        if src.name in reused:
            shutil.copyfile(reused[src.name],obj)
            return dict(source=src.name,exit_code=0,object_bytes=obj.stat().st_size,object_sha256=sha(obj),reused=True)
        flags = compile_flags + (['-Wall', '-Wextra', '-Werror'] if src.parent == pc / 'native' else [])
        # The PE data loader must not receive the SDK's register-name macros.
        forced = [] if src.name == 'pe_image.c' else ['-include', pc / 'native/fnv_sdk_runtime.h']
        command = [a.cc.resolve(), *flags, *forced]
        for directory in includes:
            command += ['-I', directory]
        command += ['-c', src, '-o', obj]
        result = subprocess.run([str(s) for s in command], capture_output=True, text=True)
        (output / (src.stem + '.log')).write_text(result.stdout + result.stderr)
        print('compiled' if result.returncode == 0 else 'FAILED', src.name, flush=True)
        return dict(source=src.name, exit_code=result.returncode,
                    object_bytes=obj.stat().st_size if result.returncode == 0 else 0,
                    object_sha256=sha(obj) if result.returncode == 0 else None)
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        compiled = list(pool.map(compile_one, sources))
    result = dict(status='COMPILE_FAILURE', compiled=compiled, compile_units=len(compiled),
                  reused_generated_objects=len(reused),
                  compiler_flags=compile_flags, x87_core_validation_sha256=x87_proof, source_sha256={str(p): h for p, h in frozen.items()},
                  sdk_sha256=sdk_hash, generation_report_sha256=sha(paths['report']),
                  native_engine_started=False, full_game_linked=False, cold_boot_supported=False)
    if all(item['exit_code'] == 0 for item in compiled):
        partial = output / 'fnv-cpu-relocatable.o'
        arguments = ['-r', '-o', partial.as_posix()] + [
            (output / (src.stem + '.o')).as_posix() for src in sources]+[obj.as_posix() for obj in x87_objects]
        if any('"' in s or '\n' in s for s in arguments):
            raise ValueError('Unsupported response-file path')
        response = output / 'link.rsp'
        response.write_text('\n'.join('"' + s + '"' for s in arguments) + '\n')
        run([a.ld.resolve(), '@' + str(response)], output / 'link.log')
        symbols = run([a.nm.resolve(), '--undefined-only', '--format=posix', '--no-demangle', partial],
                      output / 'undefined-symbols.log')
        undefined = [line.split()[0] for line in symbols.splitlines() if line.strip()]
        unexpected = sorted(set(undefined) - PLATFORM_SYMBOLS)
        # Inspect both runtime and generated code. All other units are compiled
        # with the identical explicit TLS flags, not reused generic ARM64 objects.
        tls_samples = {}
        for name in ('cpu_runtime', sources[0].stem):
            assembly = run([a.objdump.resolve(), '-dr', output / (name + '.o')], output / (name + '.asm'))
            soft_tp = '__aarch64_read_tp' in assembly
            hardware_tp = bool(re.search(r'\bmrs\b[^\n]*tpidr_el[01]', assembly, re.I))
            tls_samples[name] = dict(soft_tp_reference=soft_tp, direct_hardware_tp=hardware_tp)
        tls_ok = all(s['soft_tp_reference'] and not s['direct_hardware_tp'] for s in tls_samples.values())
        result.update(status='PASS' if not unexpected and tls_ok else 'ABI_SYMBOL_FAILURE',
                      partial_link=dict(object_bytes=partial.stat().st_size, object_sha256=sha(partial),
                                        undefined_symbols=undefined, unexpected_symbols=unexpected,
                                        final_engine_link=False), tls_samples=tls_samples)
    if (sdk_identity(sdk) != sdk_hash or verify_derivation(analysis, decoder) != selection or
            any(sha(path) != digest for path, digest in frozen.items())):
        raise ValueError('Build inputs changed during validation')
    (output / 'validation.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def recheck(a):
    """Recheck identical compiled artifacts after a validator-only update.

    Preserve the original report and require its exact archived validator;
    no runtime, generated input, SDK header, compiler or object may change.
    """
    output = a.output.resolve()
    prior_path = output / 'validation.json'
    prior = json.loads(prior_path.read_text())
    if prior.get('x87_core_validation_sha256'):raise ValueError('Extended x87 requires a fresh matched build; recheck mode is not enabled')
    if prior['status'] not in ('PASS', 'ABI_SYMBOL_FAILURE') or not a.previous_validator:
        raise ValueError('Require a completed build and its archived validator')
    tool = Path(__file__).resolve()
    rechecker_hash = sha(tool)
    previous_hash = prior['source_sha256'].get(str(tool))
    if not previous_hash or sha(a.previous_validator) != previous_hash:
        raise ValueError('Previous validator does not match the original build')
    for name, digest in prior['source_sha256'].items():
        if Path(name).resolve() != tool and sha(Path(name)) != digest:
            raise ValueError('Changed build input: ' + name)
    for binary in (a.cc, a.ld, a.nm, a.objdump):
        if prior['source_sha256'].get(str(binary.resolve())) != sha(binary):
            raise ValueError('Tool is not the pinned build input: ' + str(binary))
    paths = generation_paths(a.analysis.resolve(), a.generation_name)
    if sha(paths['report']) != prior['generation_report_sha256'] or sdk_identity(a.sdk.resolve()) != prior['sdk_sha256']:
        raise ValueError('Generation/SDK identity mismatch')
    sys.path.insert(0, str(a.python_deps.resolve()))
    import capstone
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    selection = verify_derivation(a.analysis.resolve(), decoder)
    audit = json.loads(a.audit_validation.read_text())
    validate_selection_proof(audit,selection,json.loads(paths['report'].read_text()),sha(paths['report']))
    if len(prior['compiled']) != prior['compile_units']:
        raise ValueError('Incomplete unit inventory')
    for item in prior['compiled']:
        if Path(item['source']).name != item['source'] or item['exit_code']:
            raise ValueError('Invalid/incomplete compiled unit')
        obj = output / (Path(item['source']).stem + '.o')
        if sha(obj) != item['object_sha256'] or obj.stat().st_size != item['object_bytes']:
            raise ValueError('Changed compiled object: ' + item['source'])
    partial = output / 'fnv-cpu-relocatable.o'
    if sha(partial) != prior['partial_link']['object_sha256']:
        raise ValueError('Changed relocatable link')
    symbols = run([a.nm.resolve(), '--undefined-only', '--format=posix', '--no-demangle', partial], output / 'undefined-symbols-recheck.log')
    undefined = [line.split()[0] for line in symbols.splitlines() if line.strip()]
    if undefined != prior['partial_link']['undefined_symbols']:
        raise ValueError('Symbol inventory changed')
    unexpected = sorted(set(undefined) - PLATFORM_SYMBOLS)
    samples = {}
    for name in prior['tls_samples']:
        assembly = run([a.objdump.resolve(), '-dr', output / (name + '.o')], output / (name + '-recheck.asm'))
        samples[name] = dict(soft_tp_reference='__aarch64_read_tp' in assembly,
                            direct_hardware_tp=bool(re.search(r'\bmrs\b[^\n]*tpidr_el[01]', assembly, re.I)))
    if sha(tool) != rechecker_hash or verify_derivation(a.analysis.resolve(), decoder) != selection:
        raise ValueError('Recheck inputs changed')
    for name, digest in prior['source_sha256'].items():
        if Path(name).resolve() != tool and sha(Path(name)) != digest:
            raise ValueError('Build input changed during recheck: ' + name)
    prior.update(status='PASS' if not unexpected and all(s['soft_tp_reference'] and not s['direct_hardware_tp'] for s in samples.values()) else 'ABI_SYMBOL_FAILURE',
                 rechecked_from_validation_sha256=sha(prior_path), previous_validator_sha256=previous_hash,
                 rechecker_sha256=rechecker_hash, objects_recompiled=False, tls_samples=samples)
    prior['source_sha256'][str(tool)] = rechecker_hash
    prior['partial_link']['unexpected_symbols'] = unexpected
    (output / 'revalidation.json').write_text(json.dumps(prior, indent=2) + '\n')
    return prior


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('analysis', 'sdk', 'cc', 'ld', 'nm', 'objdump', 'python-deps', 'output', 'audit-validation'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--generation-name', required=True)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--reuse-generated-validation',type=Path,
                        help='Reuse identical C units with identical headers/compiler across audited generations; rebuild changed units and native CPU/PE')
    parser.add_argument('--x87-profile',type=Path,help='Exact experimental profile recorded by extended generation')
    parser.add_argument('--x87-core-validation',type=Path,help='Current SoftFloat/x87 Switch-ABI COMPILED proof')
    parser.add_argument('--recheck', action='store_true', help='Verify identical existing objects after a validator-only change')
    parser.add_argument('--previous-validator', type=Path, help='Exact archived script recorded by the original build')
    arguments = parser.parse_args()
    result = recheck(arguments) if arguments.recheck else build(arguments)
    print(json.dumps({k: v for k, v in result.items() if k not in ('compiled', 'source_sha256')}, indent=2))
    if result['status'] != 'PASS':
        raise SystemExit(1)

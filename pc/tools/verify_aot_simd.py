"""Compile changed scalar/SIMD AOT chunks from a private named variant.

Verify identities and preserve baseline. Objects are compile evidence only;
this does not link or execute the game. Outputs include game-derived code.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import subprocess
from aot_simd_bits import SUPPORTED
from run_aot_analysis import generation_paths, sdk_identity, sha


def verify(analysis, name, sdk, compiler, output, jobs=2, baseline_name=None, mul_flags=False, simd_flags_preserve=False):
    if not 1 <= jobs <= 8: raise ValueError('Use 1..8 compile jobs')
    analysis = analysis.resolve(); sdk = sdk.resolve(); output = output.resolve()
    if output.exists(): raise ValueError('Use a fresh private validation directory')
    baseline_paths = generation_paths(analysis, baseline_name)
    variant_paths = generation_paths(analysis, name)
    if not name: raise ValueError('A separately named generation is required')
    baseline = json.loads(baseline_paths['report'].read_text())
    variant = json.loads(variant_paths['report'].read_text())
    for field in ('input_profile_sha256','sdk_sha256','index_wrapper_sha256','functions_generated'):
        if baseline[field] != variant[field]: raise ValueError('Different generation inputs/count: '+field)
    if not baseline['full_selection'] or not variant['full_selection']:
        raise ValueError('Full generation required for a baseline comparison')
    if sdk_identity(sdk) != variant['sdk_sha256']:
        raise ValueError('SDK source changed')
    pc = Path(__file__).resolve().parents[1]
    overlay = pc/'tools'/('aot_mul_flags.py' if mul_flags else 'aot_simd_bits.py')
    header = pc/'native'/('fnv_mul_flags.h' if mul_flags else 'fnv_simd_bits.h')
    codegen = variant.get('multiply_overlay' if mul_flags else 'codegen_overlay') or {}
    if sha(overlay) != codegen.get('source_sha256') or sha(header) != codegen.get('runtime_header_sha256'):
        raise ValueError('Overlay/runtime source changed since generation')
    simd_identity = None
    if mul_flags and baseline.get('codegen_overlay') != variant.get('codegen_overlay'):
        old_simd = baseline.get('codegen_overlay') or {}
        simd_identity = variant.get('codegen_overlay') or {}
        if not simd_flags_preserve or old_simd.get('runtime_header_sha256') != simd_identity.get('runtime_header_sha256'):
            raise ValueError('Multiply comparison requires the same SIMD runtime and an explicit flag-preservation change')
        if sha(pc/'tools/aot_simd_bits.py') != simd_identity.get('source_sha256') or sha(pc/'native/fnv_simd_bits.h') != simd_identity.get('runtime_header_sha256'):
            raise ValueError('SIMD flag-preservation source identity mismatch')
    if not mul_flags and baseline.get('multiply_overlay') != variant.get('multiply_overlay'):
        raise ValueError('SIMD comparison must not change the multiply overlay')
    for report, paths in ((baseline, baseline_paths), (variant, variant_paths)):
        for filename, digest in report['output_files'].items():
            if sha(paths['generated']/filename) != digest:
                raise ValueError('Generated output changed: '+str(paths['generated']/filename))
    original_names = set(baseline['output_files'])
    if original_names != set(variant['output_files']): raise ValueError('Generated file selection changed')
    changed = [n for n in sorted(original_names) if baseline['output_files'][n] != variant['output_files'][n]]
    if baseline['output_files']['recomp_dispatch.c'] != variant['output_files']['recomp_dispatch.c']:
        raise ValueError('Instruction overlay must preserve public dispatch')
    # A changed body must actually contain one of the new instructions. Avoid
    # presenting unrelated changed output as a verified SIMD transformation.
    chunks = []
    removed = Counter()
    introduced = Counter()
    allowed_removed = {'seto','setno'} if mul_flags else SUPPORTED
    all_families = set(baseline['gaps']['unsupported_instructions']) | set(variant['gaps']['unsupported_instructions'])
    for family in all_families:
        before = Counter(baseline['gaps']['unsupported_instructions'].get(family, []))
        after = Counter(variant['gaps']['unsupported_instructions'].get(family, []))
        newly_unknown = after-before
        if newly_unknown and (not mul_flags or family not in ('seto','setno')):
            raise ValueError('New unsupported occurrence: '+family)
        introduced[family] = sum(newly_unknown.values())
        difference = before-after
        if difference and family not in allowed_removed: raise ValueError('Unrelated unsupported occurrence changed: '+family)
        removed[family] = sum(difference.values())
    definition = re.compile(r'^void (\w+)\(void\)', re.M)
    for filename in changed:
        if not re.fullmatch(r'recomp_\d{4}\.c', filename):
            if filename != 'recomp_funcs.h': raise ValueError('Unexpected changed artifact: '+filename)
            continue
        before = (baseline_paths['generated']/filename).read_text()
        after = (variant_paths['generated']/filename).read_text()
        if definition.findall(before) != definition.findall(after):
            raise ValueError('Function body selection changed: '+filename)
        markers = ('/* fnv mul: product and CF/OF */','/* fnv imul: product and CF/OF */') if mul_flags else tuple('/* '+m+' */' for m in SUPPORTED)
        if not any(marker in after for marker in markers) and not (mul_flags and simd_flags_preserve):
            raise ValueError('Changed chunk lacks expected instruction emissions: '+filename)
        chunks.append(filename)
    if not chunks or not sum(removed.values()): raise ValueError('No actual instruction coverage change')
    output.mkdir(parents=True)
    flags = ['-std=gnu11','-O1','-fno-strict-aliasing','-ffp-contract=off','-mearly-ra=none',
             '-Werror=implicit-function-declaration']
    def compile_chunk(filename):
        source = variant_paths['generated']/filename
        obj = output/(source.stem+'.o')
        invocation = [str(compiler.resolve()), *flags, '-I',str(sdk/'templates/runtime'),
            '-I',str(sdk/'src/kernel'), '-I',str(pc/'native'), '-I',str(variant_paths['generated']),
            '-c',str(source), '-o',str(obj)]
        result = subprocess.run(invocation, capture_output=True, text=True)
        (output/(source.stem+'.log')).write_text(result.stdout+result.stderr)
        return dict(source=filename, exit_code=result.returncode,
            object_sha256=sha(obj) if result.returncode == 0 else None,
            object_bytes=obj.stat().st_size if result.returncode == 0 else 0)
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        compiled = list(pool.map(compile_chunk, chunks))
    if sdk_identity(sdk) != variant['sdk_sha256'] or sha(overlay) != codegen['source_sha256'] or \
            sha(header) != codegen['runtime_header_sha256']:
        raise ValueError('Compile input identity changed during validation')
    if simd_identity and (sha(pc/'tools/aot_simd_bits.py') != simd_identity['source_sha256'] or
            sha(pc/'native/fnv_simd_bits.h') != simd_identity['runtime_header_sha256']):
        raise ValueError('SIMD input changed during validation')
    result = dict(status='PASS' if all(c['exit_code'] == 0 for c in compiled) else 'COMPILE_FAILURE',
        baseline_name=baseline_name, variant_name=name, overlay_kind='mul-flags' if mul_flags else 'simd-bits',
        baseline_report_sha256=sha(baseline_paths['report']), variant_report_sha256=sha(variant_paths['report']),
        generated_functions=variant['functions_generated'], changed_chunks=len(chunks),
        unchanged_files=len(original_names)-len(changed),
        removed_unsupported_occurrences={k:v for k,v in sorted(removed.items()) if v},
        newly_unresolved_flag_occurrences={k:v for k,v in sorted(introduced.items()) if v},
        simd_flag_preservation_changed=simd_flags_preserve,
        remaining_unsupported_occurrences=sum(len(v) for v in variant['gaps']['unsupported_instructions'].values()),
        unresolved_targets=variant['gaps']['unresolved_call_targets'], dispatch_preserved=True,
        compiler_flags=flags, compile_jobs=jobs, compiled=compiled,
        full_game_linked=False, native_engine_started=False, cold_boot_supported=False)
    (output/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--analysis', type=Path, required=True)
    parser.add_argument('--generation-name', required=True)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--cc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--baseline-name', help='Compare to a named accepted generation rather than the original baseline')
    parser.add_argument('--mul-flags', action='store_true', help='Verify a multiply-only change against the same SIMD baseline')
    parser.add_argument('--simd-flags-preserve', action='store_true', help='Also accept the verified SIMD flag-tracking update (same runtime helpers)')
    args = parser.parse_args()
    result = verify(args.analysis,args.generation_name,args.sdk,args.cc,args.output,args.jobs,args.baseline_name,args.mul_flags,args.simd_flags_preserve)
    print(json.dumps({k:v for k,v in result.items() if k != 'compiled'},indent=2))
    if result['status'] != 'PASS': raise SystemExit(1)

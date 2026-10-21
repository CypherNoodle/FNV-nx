"""Regenerate explicitly selected non-x87 functions in a frozen private unit.

Writes a new unit/proof; never edits the SDK, old generated unit or manifest.
Does not compile, relabel reused objects, or establish a runnable game.
"""
import argparse
import json
from pathlib import Path
import re
import sys


def replace_function(unit, address, body):
    name = f'sub_{address:08X}'
    pattern = rf'^void {name}\(void\)\n\{{.*?(?=^void sub_|\Z)'
    matches = list(re.finditer(pattern, unit, re.M | re.S))
    definitions = list(re.finditer(r'^void sub_[0-9A-Fa-f]+\(void\)\n\{', body, re.M))
    if len(matches) != 1 or len(definitions) != 1 or definitions[0].group() != f'void {name}(void)\n{{':
        raise ValueError('Require exactly one matching function definition')
    selected = matches[0]
    return unit[:selected.start()] + body.rstrip() + '\n\n' + unit[selected.end():]


def regenerate(a):
    from x87_target_evaluator import sha, unchanged
    from run_aot_analysis import sdk_identity
    if a.output.exists():
        raise ValueError('Fresh private output required')
    baseline = json.loads(a.baseline.read_text())
    if baseline.get('status') != 'EXPERIMENT_STOPPED' or not baseline.get('all_generated'):
        raise ValueError('Require a stopped complete diagnostic')
    unchanged(baseline['source_sha256'])
    unit = a.unit.resolve()
    if baseline['source_sha256'].get(str(unit)) != sha(unit):
        raise ValueError('Unit is not frozen in the diagnostic')
    if sdk_identity(a.sdk.resolve()) != baseline['sdk_sha256']:
        raise ValueError('SDK identity changed')
    image = a.analysis / 'fnv-analysis.xbe'
    if sha(image) != baseline['identity']['analysis_container_sha256']:
        raise ValueError('Analysis container changed')
    database = a.analysis / 'disasm/functions.json'
    if baseline['source_sha256'].get(str(database.resolve())) != sha(database):
        raise ValueError('Function database changed')
    sys.path[:0] = [str(a.sdk.resolve()), str(a.python_deps.resolve())]
    from tools.recomp import config
    from tools.recomp.translator import FunctionTranslator
    from aot_simd_bits import install as simd
    from aot_mul_flags import install as multiply
    from aot_frame_restore import install as frame
    from aot_decode_resync import install as resync
    pc = Path(__file__).resolve().parents[1]
    tools = [pc / 'tools' / name for name in ('regenerate_decode_unit.py',
             'aot_decode_resync.py', 'aot_simd_bits.py', 'aot_mul_flags.py',
             'aot_frame_restore.py', 'run_aot_analysis.py', 'x87_target_evaluator.py')]
    frozen = {str(p.resolve()): sha(p) for p in [*tools, a.baseline, unit, image, database]}
    config.configure_from_xbe(str(image)); simd(); multiply(); frame(); resync()
    db = {int(f['start'], 0): dict(f) for f in json.loads(database.read_text())}
    translator = FunctionTranslator(image.read_bytes(), db)
    original = unit.read_text(); current = original; changes = []
    for address in sorted(set(a.function)):
        if address not in db:
            raise ValueError('Function is absent from audited database')
        info = dict(db[address]); info['end'] = int(info['end'], 0) if isinstance(info['end'], str) else info['end']
        body = translator.translate_function(address, info)
        if any(token in body for token in ('translation failed', ': unhandled */', 'fnv_x87_', 'g_fp_', 'FP_PUSH', 'FP_POP')):
            raise ValueError('This bounded regeneration does not support failed/x87 bodies')
        body = body.replace('RECOMP_ICALL_SAFE_AT(', 'FNV_STARTUP_ICALL(')
        current = replace_function(current, address, body)
        changes.append(dict(address=address, end=info['end']))
    if not changes or current == original:
        raise ValueError('No effective explicitly selected regeneration')
    unchanged(frozen); unchanged(baseline['source_sha256'])
    a.output.mkdir(parents=True)
    target = a.output / unit.name; target.write_text(current, newline='\n')
    report = dict(status='REGENERATED_UNTESTED', selected=changes,
        source_sha256=frozen, original_sha256=sha(unit), output_sha256=sha(target),
        sdk_sha256=baseline['sdk_sha256'], unrelated_bodies_preserved=True,
        compiled=False, engine_started=False, switch_hardware_tested=False)
    (a.output / 'validation.json').write_text(json.dumps(report, indent=2)+'\n', newline='\n')
    print('REGENERATED_UNTESTED', len(changes), 'explicit function(s)')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'analysis', 'unit', 'sdk', 'python-deps', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--function', type=lambda value:int(value,0), action='append', required=True)
    regenerate(p.parse_args())

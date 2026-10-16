"""Audit hashed private D3D9 blobs with the external SPIR-V translator.

Translation acceptance is NOT linked-module validation or rendered coverage.
The output never contains shader bytecode; originals stay in the private input.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess
from translate_d3d9_pair import normalize


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def audit(args):
    manifest, raw = args.manifest.resolve(), args.raw.resolve()
    hashes = manifest.read_text(encoding='ascii').splitlines()
    if not 0 < len(hashes) <= 20000 or len(hashes) != len(set(hashes)):
        raise ValueError('empty, duplicate or excessive manifest')
    for key in hashes:
        if not re.fullmatch('[0-9a-f]{64}', key):
            raise ValueError('invalid shader hash')
        path = raw/(key+'.bin')
        if not 8 <= path.stat().st_size <= 1024*1024 or sha(path) != key:
            raise ValueError('shader identity/size mismatch: '+key)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    normalized = {}
    input_manifest, input_raw = manifest, raw
    if args.centroid:
        input_raw = out/'normalized'
        input_raw.mkdir()
        for key in hashes:
            data, declarations = normalize((raw/(key+'.bin')).read_bytes())
            target = hashlib.sha256(data).hexdigest()
            normalized[key] = {'hash': target, 'centroid': declarations}
            path = input_raw/(target+'.bin')
            if not path.exists():
                path.write_bytes(data)
        input_manifest = out/'normalized-manifest.txt'
        input_manifest.write_text('\n'.join(sorted({v['hash'] for v in normalized.values()}))+'\n', encoding='ascii')
        (out/'normalization.json').write_text(json.dumps(normalized, indent=2)+'\n', encoding='utf-8')
    with (out/'results.jsonl').open('w', encoding='utf-8') as results, \
         (out/'errors.txt').open('w', encoding='utf-8') as errors:
        subprocess.run([str(args.linker.resolve()), '--audit-manifest', str(input_manifest), str(input_raw)],
                       stdout=results, stderr=errors, check=True, timeout=180)
    rows = [json.loads(line) for line in (out/'results.jsonl').read_text(encoding='utf-8').splitlines()]
    expected = input_manifest.read_text(encoding='ascii').splitlines()
    if [row['hash'] for row in rows] != expected:
        raise ValueError('audit output does not match manifest')
    causes = Counter()
    for line in (out/'errors.txt').read_text(encoding='utf-8').splitlines():
        match = re.search(r'\.bin:-?\d+: (.*)$', line)
        if match:
            causes[match.group(1)] += 1
    if sum(causes.values()) != sum(row['error_count'] for row in rows):
        raise ValueError('unclassified translator errors')
    unique_translated = sum(row['translated'] for row in rows)
    by_hash = {row['hash']: row for row in rows}
    accepted = sum(by_hash[normalized[key]['hash']]['translated'] for key in hashes) if args.centroid else unique_translated
    report = {'schema': 1, 'status': 'COMPLETE', 'total': len(rows),
              'translation_accepted': accepted, 'translation_rejected': len(hashes)-accepted,
              'total_original': len(hashes), 'unique_translator_inputs': len(rows),
              'centroid_normalization': args.centroid,
              'centroid_reapplied': False,
              'shaders_with_centroid': sum(bool(v['centroid']) for v in normalized.values()),
              'error_occurrences': dict(causes), 'linked_pairs_checked': 0,
              'gpu_rendered': False, 'game_executed': False,
              'switch_hardware_tested': False, 'input_sha256': {
                  'manifest': sha(manifest), 'linker': sha(args.linker), 'audit_tool': sha(Path(__file__))},
              'results_sha256': sha(out/'results.jsonl'), 'errors_sha256': sha(out/'errors.txt')}
    report['input_sha256']['centroid_adapter'] = sha(Path(normalize.__code__.co_filename))
    (out/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('manifest', 'raw', 'linker', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--centroid', action='store_true', help='Audit temporary normalization only, not linked/restored coverage')
    audit(p.parse_args())

"""Merge independently rechecked gap entries into a private x86 function list.

This produces translator input, not generated C or a runtime implementation.
Keep image bytes, audit reports and the resulting function list under local/.
"""
import argparse
import hashlib
import json
from pathlib import Path
from audit_gap_function import audit


def merge(code, base, functions_bytes, reports):
    functions = json.loads(functions_bytes)
    functions_hash = hashlib.sha256(functions_bytes).hexdigest()
    code_hash = hashlib.sha256(code).hexdigest()
    additions = []
    checked_fields = ('start', 'end', 'size', 'instructions', 'reachable_bytes',
                      'returns', 'calls', 'indirect_calls', 'tail_exits')
    for report in reports:
        if (report.get('status') != 'AUDITED_GAP_ENTRY' or
                report.get('functions_sha256') != functions_hash or
                report.get('code_sha256') != code_hash):
            raise ValueError('Audit identity does not match frozen inputs')
        checked = audit(code, base, report['start'], report['end'], functions)
        if any(report.get(key) != checked[key] for key in checked_fields):
            raise ValueError('Audit differs from independent CFG reconstruction')
        if any(a < checked['end'] and checked['start'] < b for a, b in additions):
            raise ValueError('Audited additions overlap or repeat')
        additions.append((checked['start'], checked['end']))
    if not additions:
        raise ValueError('No audited entries supplied')
    result = list(functions)
    for start, end in additions:
        result.append(dict(start=hex(start), end=hex(end), size=end-start,
                           name='sub_%08X' % start, section='.text',
                           detection_method='audited_observed_gap'))
    return sorted(result, key=lambda row: int(row['start'], 0)
                  if isinstance(row['start'], str) else row['start'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('code', 'functions', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--base', type=lambda value: int(value, 0), required=True)
    parser.add_argument('--audit', type=Path, action='append', required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError(args.output)
    result = merge(args.code.read_bytes(), args.base, args.functions.read_bytes(),
                   [json.loads(path.read_text()) for path in args.audit])
    with args.output.open('x', encoding='ascii', newline='\n') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')

"""Frozen identities, independent reconstruction and atomic list additions."""
import copy
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from audit_gap_function import audit
from merge_gap_audits import merge


def test():
    code = bytes.fromhex('c3ccccccc3')
    functions = json.dumps([dict(start='0x1002', end='0x1003')]).encode()
    rows = json.loads(functions)
    reports = [audit(code, 0x1000, start, start+1, rows)
               for start in (0x1000, 0x1004)]
    for report in reports:
        report['functions_sha256'] = hashlib.sha256(functions).hexdigest()
    result = merge(code, 0x1000, functions, reports)
    assert len(result) == 3 and result[1] == rows[0]
    assert result[0]['start'] == '0x1000' and result[2]['end'] == '0x1005'
    broken = []
    for key, value in [('status', 'UNVERIFIED'), ('code_sha256', 'bad'),
                       ('functions_sha256', 'bad'), ('size', 99),
                       ('instructions', 99), ('returns', []),
                       ('end', 0x1002), ('start', 0x1002)]:
        changed = copy.deepcopy(reports[0]); changed[key] = value
        broken.append([changed])
    broken.extend([[], [reports[0], reports[0]]])
    for candidates in broken:
        try:
            merge(code, 0x1000, functions, candidates)
        except ValueError:
            pass
        else:
            raise AssertionError('Unverified or duplicate addition accepted')
    try:
        merge(code, 0x1000, functions+b' ', reports)
    except ValueError:
        pass
    else:
        raise AssertionError('Function-list byte identity was not checked')
    print('PASS two rechecked additions, original rows preserved, eleven rejections')


if __name__ == '__main__':
    test()

"""Independent Decimal reference for selected ordinary candidate results/C1.

Consumes only frozen synthetic test output, never private game artifacts.
Does not authorize AOT integration or broaden the supported operand domain.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    proof=json.loads(a.validation.read_text());base=a.validation.resolve().parent
    if proof['status'] not in ('CANDIDATE_WITH_C1_DIFFERENCES','CANDIDATE_MATCHED_SELECTED_CASES') or proof['failures'] or proof['exception_or_stack_flag_failures'] or proof['negative_failures']:raise ValueError('Completed candidate test required')
    for path,h in proof['source_sha256'].items():
        if sha(Path(path))!=h:raise ValueError('Stale synthetic test')
    for name,h in proof['generated_sha256'].items():
        if sha(base/name)!=h:raise ValueError('Changed synthetic output')
    data=(base/'inputs.bin').read_bytes();actual=(base/'actual.bin').read_bytes();native=(base/'expected.bin').read_bytes()
    if len(data)!=24*proof['cases'] or len(actual)!=32*proof['cases'] or len(native)!=24*proof['cases']:raise ValueError('Wrong record sizes')
    tool=Path(__file__).resolve().parents[1]/'tools/x87_transcendental_decimal.py'
    frozen={str(p):sha(p) for p in (Path(__file__).resolve(),tool,a.validation.resolve())}
    sys.path.insert(0,str(tool.parent));from x87_transcendental_decimal import reference
    failures=[];counts=Counter();native_c1=0;native_values=0
    def matches(payload,sw,want):return payload==want[0] and bool(sw&0x200)==bool(want[1])
    for index in proof.get('math_reference_case_indices',proof['ordinary_case_indices']):
        record=data[24*index:24*(index+1)];cw=int.from_bytes(record[20:22],'little');op=record[22]
        wanted=reference(op,record[:10],record[10:20])[(cw>>10)&3]
        row=actual[32*index:32*(index+1)];sw=int.from_bytes(row[20:22],'little')
        if not matches(row[:10],sw,wanted):failures.append(dict(case=index,op=op,actual=row[:10].hex(),expected=wanted[0].hex(),actual_c1=bool(sw&0x200),expected_c1=wanted[1]))
        reference_row=native[24*index:24*(index+1)];native_c1+=bool(int.from_bytes(reference_row[20:22],'little')&0x200)!=bool(wanted[1]);native_values+=reference_row[:10]!=wanted[0]
        counts[op]+=1
    if not counts:raise ValueError('No ordinary cases')
    # Both corruptions must be rejected by the same acceptance predicate.
    wanted=reference(1,struct.pack('<QH',1<<63,0x3fff),struct.pack('<QH',1<<63,0x3fff))[0]
    wrong_payload=bytes([wanted[0][0]^1])+wanted[0][1:]
    for payload,flag in ((wrong_payload,wanted[1]),(wanted[0],1-wanted[1])):
        if matches(payload,flag<<9,wanted):raise ValueError('Negative control accepted')
    for path,h in {**proof['source_sha256'],**frozen}.items():
        if sha(Path(path))!=h:raise ValueError('Reference input changed during test')
    for name,h in proof['generated_sha256'].items():
        if sha(base/name)!=h:raise ValueError('Synthetic output changed during test')
    result=dict(status='PASS_SELECTED_MATHEMATICAL_MODEL' if not failures else 'MATH_MODEL_FAILED',
                cases=sum(counts.values()),per_operation=dict(counts),decimal_precisions=[210,310],
                failures=failures,native_c1_disagreements=native_c1,native_payload_disagreements=native_values,
                negative_controls=2,source_sha256=frozen,
                production_translation_enabled=False,switch_hardware_tested=False,
                limits=['Ordinary bounded normal test cases only','Mathematical C1 is not a physical x87 microcode trace','No new supported domain or game boot claim'])
    a.output.mkdir(parents=True);(a.output/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='failures'},indent=2));print('failures',len(failures))
    return 1 if failures else 0


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('validation','output'):parser.add_argument('--'+name,type=Path,required=True)
    raise SystemExit(test(parser.parse_args()))

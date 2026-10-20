"""Synthetic CFG controls: no game inputs or compiler needed."""
import argparse
import json
from pathlib import Path
import sys


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    sys.path[:0]=[str(a.python_deps.resolve()),str(Path(__file__).resolve().parents[1]/'tools')]
    from capstone import Cs,CS_ARCH_X86,CS_MODE_32
    from audit_x87_c1_liveness import analyze,sha
    decoder=Cs(CS_ARCH_X86,CS_MODE_32);decoder.detail=True
    cases=[('d9e8',True),('db2d00102000',True),('d9cb',True),
           ('7402d9e8d9eb',True),('7402d9e8c3',False),('7402d9e8dfe0',False),
           ('dfe0',False),('d93500102000',False),('e800000000d9e8',False),
           ('c3',False),('ebfe',False),('ffe0',False),('d9f3',False),
           ('90',False),('cd80',False),('7402d9e890ebfd',False),
           ('9bd9e8',True),('0fae00',False),('0f0bd9e8',False),('f4d9e8',False),('0f05d9e8',False)]
    for index,(encoded,expected) in enumerate(cases):
        data=bytes.fromhex(encoded);base=0x400000
        def reader(va):return data[va-base:va-base+15] if base<=va<base+len(data) else None
        result=analyze(reader,base,decoder)
        if (result['status']=='ALL_PATHS_OVERWRITE_C1')!=expected:raise RuntimeError(str((index,encoded,result)))
    data=bytes.fromhex('909090d9e8');base=0x400000
    def reader(va):return data[va-base:] if base<=va<base+len(data) else None
    if analyze(reader,base,decoder,budget=2)['status']!='C1_NOT_PROVEN_DEAD':raise RuntimeError('Budget accepted')
    paths=[Path(__file__).resolve(),Path(__file__).resolve().parents[1]/'tools/audit_x87_c1_liveness.py']
    result=dict(status='PASS',synthetic_cases=len(cases)+1,source_sha256={str(p):sha(p) for p in paths},game_inputs_used=False)
    a.output.mkdir(parents=True);(a.output/'validation.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('python-deps','output'):parser.add_argument('--'+name,type=Path,required=True)
    test(parser.parse_args())

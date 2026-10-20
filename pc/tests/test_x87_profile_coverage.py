"""A numeric emitter's acceptance must not hide production addressing rejects."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
from pe_fixture import write_pe


def test(a):
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True);out=a.output.resolve();pc=Path(__file__).resolve().parents[1]
    sys.path.insert(0,str(pc/'tools'))
    from prepare_aot_image import prepare
    from run_aot_analysis import run,sha
    from x87_models import TRIG_MODEL
    inputs=[Path(__file__).resolve(),pc/'tests/pe_fixture.py',pc/'tools/audit_x87_coverage.py',
            pc/'tools/aot_x87_extended.py',pc/'tools/aot_x87_codegen.py']
    frozen={p:sha(p) for p in inputs}
    exe=out/'fixture.exe';write_pe(exe);raw=bytearray(exe.read_bytes())
    struct.pack_into('<I',raw,0x98+224+36,0x60000020)
    # ES is opt-in; FS/GS and 16-bit addressing stay rejected. Never executed.
    code=bytes.fromhex('d9e826d80064d80065d80067d800d9fec3');raw[0x200:0x200+len(code)]=code;exe.write_bytes(raw)
    analysis=out/'analysis';prepare(exe,analysis,sha(exe))
    run(analysis,a.sdk,a.python_deps,stage='scan')
    common=dict(schema='fnv-x87-protected32-v1',contract_status='experimental',code_selector=35,
                data_selector=43,stack_selector=43,opcode_compatibility=True,data_pointer_on_exception_only=False)
    results={}
    for name,profile,expected in [('strict',common,{'fadd':4,'fsin':1}),
        ('trig',dict(common,schema='fnv-x87-protected32-v2',transcendental_model=TRIG_MODEL),{'fadd':4}),
        ('es-trig',dict(common,schema='fnv-x87-protected32-v3',extra_selector=43,transcendental_model=TRIG_MODEL),{'fadd':3})]:
        pp=out/(name+'.json');pp.write_text(json.dumps(profile));dest=out/(name+'-coverage')
        r=subprocess.run([sys.executable,str(pc/'tools/audit_x87_coverage.py'),'--analysis',str(analysis),
            '--sdk',str(a.sdk.resolve()),'--python-deps',str(a.python_deps.resolve()),
            '--x87-profile',str(pp),'--output',str(dest)],capture_output=True,text=True)
        (out/(name+'.log')).write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(r.stderr)
        proof=json.loads((dest/'validation.json').read_text())
        if proof['coverage_layer']!='production-profile' or proof['unsupported_mnemonics']!=expected:
            raise AssertionError('Production addressing or model restriction was hidden')
        results[name]=expected
    if any(sha(p)!=h for p,h in frozen.items()):raise ValueError('Test source changed')
    report=dict(status='PASS',profile_rejections=results,production_segment_guard_preserved=True,
                source_sha256={str(p):h for p,h in frozen.items()},game_executed=False)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('sdk','python-deps','output'):p.add_argument('--'+name,type=Path,required=True)
    test(p.parse_args())

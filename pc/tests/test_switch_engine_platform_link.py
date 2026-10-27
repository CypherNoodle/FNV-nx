"""Reject invalid platform-link evidence before any output/compiler activity."""
import argparse
import copy
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from link_switch_engine_platform import build

def run():
    with tempfile.TemporaryDirectory(prefix='engine-link-negative-',dir=Path.cwd()) as tmp:
        root=Path(tmp);runtime=root/'runtime.json'
        original=dict(status='PARTIAL_LINKED',missing_symbols=[],generated_units=359,
                      supplementary_entries=2,inputs=dict(runtime_validation=str(runtime)))
        cases=[]
        for name,value in [('status','FAILED'),('generated_units',3),('missing_symbols',['unresolved']),('supplementary_entries',0)]:
            cpu=copy.deepcopy(original);cpu[name]=value;cases.append((cpu,dict(status='PASS',native_units=51),dict(status='PASS',static_vulkan=True)))
        cpu=copy.deepcopy(original);cpu['inputs']['runtime_validation']=str(root/'wrong.json')
        cases.append((cpu,dict(status='PASS',native_units=51),dict(status='PASS',static_vulkan=True)))
        cases.append((original,dict(status='PASS',native_units=51),dict(status='PASS',static_vulkan=False)))
        for index,(cpu,rt,mesa) in enumerate(cases):
            cpu_path=root/'cpu.json';mesa_path=root/'mesa.json';out=root/f'out-{index}'
            for path,value in ((cpu_path,cpu),(runtime,rt),(mesa_path,mesa)):path.write_text(json.dumps(value))
            args=SimpleNamespace(output=out,cpu_validation=cpu_path,runtime_validation=runtime,mesa_validation=mesa_path)
            try:build(args)
            except ValueError:pass
            else:raise AssertionError('Invalid link evidence was accepted')
            assert not out.exists()
    print('PASS 6 invalid platform proofs rejected before output/compiler access')

if __name__=='__main__':run()

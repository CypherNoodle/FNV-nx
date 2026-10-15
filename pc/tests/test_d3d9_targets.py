"""Actual translated surface/target/Clear calls against a private D3D9 HAL."""
import argparse
from pathlib import Path
from test_d3d9_com import test,SLOTS
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output','shaders'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    test(p.parse_args(),host_source='d3d9_targets_host.c',
        slots=SLOTS+[(28,9),(29,9),(12,2),(37,3),(38,3),(39,2),(40,2),(43,7),(10,1),(47,2),(48,2)],
        modes=('normal','unknown','unsupported','stale','unregistered','parent','capacity','alias','target_parent','target_provider'),
        strict_fault_cases=5,extra_inputs=(Path(__file__).resolve(),))

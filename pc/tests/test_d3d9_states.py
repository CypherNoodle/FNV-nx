"""Compare translated COM state calls, exact bits and failure paths to D3D9."""
import argparse
from pathlib import Path
from test_d3d9_com import test,SLOTS
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output','shaders'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    test(p.parse_args(),host_source='d3d9_states_host.c',
        slots=SLOTS+[(57,3),(58,3),(69,4),(68,4),(47,2),(48,2),(75,2),(76,2)],
        modes=('normal','unknown','unsupported','stale','unregistered','parent','capacity','alias','state_provider'),
        strict_fault_cases=5,extra_inputs=(Path(__file__).resolve(),))

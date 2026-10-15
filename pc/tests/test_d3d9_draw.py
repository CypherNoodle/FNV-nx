"""A complete own textured pipeline through translated COM callers, desktop HAL."""
import argparse
import os
from pathlib import Path
from test_d3d9_com import test,SLOTS
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output','shaders'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    test(p.parse_args(),host_source='d3d9_draw_host.c',slots=SLOTS+[
        (41,1),(42,1),(81,4),(82,7),(26,7),(27,7),(11,5),(12,1),(100,5),(104,2),(86,3),(87,2),
        (23,9),(19,5),(20,2),(65,3),(28,9),(37,3),(29,9),(39,2),(43,7),(57,3),(69,4),(107,2),(101,5),(105,2)],
        modes=('normal','unknown','unsupported','stale','unregistered','parent','capacity','alias','draw_provider'),
        strict_fault_cases=5,extra_inputs=(Path(__file__).resolve(),
            Path(os.environ['SystemRoot'])/'System32/d3dcompiler_47.dll',
            Path(os.environ['SystemRoot'])/'System32/d3d9.dll'))

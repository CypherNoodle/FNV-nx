"""Translated texture COM calls versus Windows D3D9 NULLREF; no GPU draws."""
import argparse
from pathlib import Path
from test_d3d9_com import test,SLOTS

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','output','shaders'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True)
    test(p.parse_args(),host_source='d3d9_textures_host.c',
        slots=SLOTS+[(23,9),(13,1),(17,3),(19,5),(20,2),(65,3),(64,3),(10,1),(18,3),(13,4),(14,1),(12,2)],
        modes=('normal','unknown','unsupported','stale','unregistered','parent','capacity','alias','texture_parent','texture_nested','texture_format'),
        strict_fault_cases=6,extra_inputs=(Path(__file__).resolve(),))

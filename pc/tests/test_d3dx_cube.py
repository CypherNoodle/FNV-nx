"""Own synthetic cube DDS through translated COM and native Windows D3DX."""
import argparse
from pathlib import Path
from test_d3d9_com import test,SLOTS
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('sdk','python-deps','output','shaders'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--cc',nargs='+',required=True);a=p.parse_args();pc=Path(__file__).resolve().parents[1]
 test(a,host_source='d3dx_cube_host.c',slots=SLOTS+[(25,8),(13,1),(17,3),(19,6),(20,3),(65,3),(64,3),(10,1),(None,4)],modes=('normal',),image_import='cube',strict_fault_cases=0,baseline_lifetime_checks=False,extra_inputs=(Path(__file__).resolve(),pc/'tests/d3d9_textures_host.c',Path('C:/Windows/System32/d3dx9_38.dll')))

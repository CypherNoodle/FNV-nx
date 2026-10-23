"""Translated D3DX DDS creation, native pixel oracle and failed-upload rollback."""
import argparse
from pathlib import Path
from test_d3d9_com import test, SLOTS

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'python-deps', 'output', 'shaders'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--cc', nargs='+', required=True)
    a = p.parse_args()
    pc = Path(__file__).resolve().parents[1]
    test(a, host_source='d3dx_texture_host.c',
         slots=SLOTS+[(23,9),(13,1),(17,3),(19,5),(20,2),(65,3),(64,3),(10,1),(None,4)],
         modes=('normal','dds_shape','dds_mips'), image_import=True,
         strict_fault_cases=2, baseline_lifetime_checks=False,
         extra_inputs=(Path(__file__).resolve(),pc/'tests/d3d9_textures_host.c',Path('C:/Windows/System32/d3dx9_38.dll')))

"""XRGB implicit backbuffer without automatic depth, through translated COM."""
import argparse
import json
from pathlib import Path
from test_d3d9_com import test
from test_d3d9_vk_backend import VK_SLOTS

def run(a):
    pc=Path(__file__).resolve().parents[1]
    sources=[pc/'native'/n for n in ('d3d9_vk_backend.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_geometry.c','d3d9_vk_state.c')]
    inputs=[Path(__file__).resolve(),pc/'tests/d3d9_vk_backend_host.c',*[p for p in (pc/'tests').glob('vk_backend*.h')],*[p for p in (pc/'native').iterdir() if p.suffix in ('.h','.inc')]]
    test(a,host_source='presentation_profile_host.c',slots=VK_SLOTS,modes=('normal','hardware'),strict_fault_cases=0,extra_inputs=inputs,extra_sources=sources,extra_includes=(a.headers,),baseline_lifetime_checks=False,factory_import=True)
    result=json.loads((a.output/'validation.json').read_text())
    hw=json.loads((a.output/'hardware.log').read_text().strip().splitlines()[-1])
    assert not result['hardware_creation'] and hw['hardware_creation'] and hw['test_provider_caps_injected']
    for r in (result,hw):
        assert r['xrgb_backbuffer'] and r['automatic_depth_disabled'] and r['reset_preserves_format'] and r['pixel_checks']==2048 and not r['production_caps_changed']
    result['hardware_fixture']=hw
    (a.output/'validation.json').write_text(json.dumps(result,indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','headers','shaders','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

"""Run the exact portable Switch probe checks through desktop Vulkan first."""
import argparse
import json
from pathlib import Path
from test_d3d9_com import test
from test_d3d9_vk_backend import VK_SLOTS

def run(a):
    pc=Path(__file__).resolve().parents[1];linked=a.linked_shaders.resolve()
    extra=[Path(__file__).resolve(),pc/'tests/test_d3d9_vk_backend.py',pc/'tests/d3d9_vk_backend_host.c',pc/'switch/renderer_probe_checks.h',*sorted((pc/'tests').glob('vk_backend_*fixture.h')),*sorted((pc/'native').glob('*.h')),*sorted((pc/'native').glob('*.inc'))]
    test(a,host_source='renderer_probe_checks_host.c',slots=VK_SLOTS,modes=('normal','present'),strict_fault_cases=0,extra_inputs=extra,extra_args=(linked/'normal.vs.spv',linked/'normal.ps.spv'),extra_sources=tuple(pc/'native'/n for n in ('d3d9_vk_backend.c','d3d9_texture_upload.c','d3d9_uniform_state.c','d3d9_uniform_pack.c','d3d9_geometry.c','d3d9_vk_state.c')),extra_includes=(a.headers.resolve(),),baseline_lifetime_checks=False,factory_import=True)
    path=a.output/'validation.json';proof=json.loads(path.read_text());wsi=json.loads((a.output/'present.log').read_text().splitlines()[-1])
    for report in (proof,wsi):assert report['probe_groups']==9 and report['probe_pixels']==10240 and report['probe_draws']==9 and report['all_gpu_handles_released']
    assert wsi['com_wsi_presents']==4;proof['translated_com_present']={'com_wsi_presents':4};proof['shared_probe_checks']=True;path.write_text(json.dumps(proof,indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','headers','shaders','linked-shaders','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

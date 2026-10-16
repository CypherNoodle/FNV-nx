"""Translated COM target/Clear calls -> actual native Vulkan provider, no NULLREF."""
import argparse
import json
import os
from pathlib import Path
from test_d3d9_com import test,SLOTS

def run(args):
    pc=Path(__file__).resolve().parents[1];headers=args.headers.resolve()
    extra=[Path(__file__).resolve(),pc/'native/d3d9_vk_textures.inc',pc/'native/d3d9_texture_upload.h',pc/'native/d3d9_vk_backend.h',pc/'native/host_mutex.h',pc/'tests/vk_backend_fault_proxy.h',pc/'tests/vk_backend_buffers_fixture.h',pc/'tests/vk_backend_textures_fixture.h',
           Path(os.environ['SystemRoot'])/'System32/vulkan-1.dll',
           *sorted((headers/'vulkan').glob('*.h')),*sorted((headers/'vk_video').glob('*.h'))]
    test(args,host_source='d3d9_vk_backend_host.c',slots=SLOTS+[(28,9),(29,9),(12,2),(37,3),(38,3),(39,2),(40,2),(43,7),(10,1),(47,2),(48,2),
        (75,2),(76,2),(57,3),(58,3),(41,1),(42,1),(26,7),(27,7),(11,5),(12,1),(100,5),(104,2),(102,3),(101,5),(105,2),(23,9),(19,5),(20,2),(65,3),(64,3),(13,1),(17,3)],modes=('normal',),strict_fault_cases=0,extra_inputs=extra,
        extra_sources=(pc/'native/d3d9_vk_backend.c',pc/'native/d3d9_texture_upload.c'),extra_includes=(headers,),baseline_lifetime_checks=False)
    report=json.loads((args.output/'validation.json').read_text());assert report['native_vulkan_backend'] and report['desktop_d3d9_used'] is False
    assert report['pixel_checks']==2304 and report['submissions']==report['completions'] and report['frames_in_flight']==2
    assert report['fault_scenarios']==9 and report['unknown_completion_quarantined'] and report['all_gpu_handles_released'] and report['noncoherent_invalidation_checked']
    assert report['texture_submit_faults']==3 and report['texture_upload_faults']==5 and report['texture_bytes_checked']>=500 and report['immutable_texture_versions']
    assert report['buffer_upload_faults']==3 and report['immutable_buffer_versions'] and report['buffer_bytes_checked']>=800

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','headers','shaders','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

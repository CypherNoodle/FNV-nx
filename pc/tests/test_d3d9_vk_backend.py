"""Translated COM target/Clear calls -> actual native Vulkan provider, no NULLREF."""
import argparse
import json
import os
import subprocess
from pathlib import Path
from test_d3d9_com import test,SLOTS

def run(args):
    pc=Path(__file__).resolve().parents[1];headers=args.headers.resolve();linked=args.linked_shaders.resolve()
    extra=[Path(__file__).resolve(),pc/'native/d3d9_factory.inc',pc/'native/d3d9_vk_draw.inc',pc/'native/d3d9_geometry.h',pc/'native/d3d9_vk_state.h',pc/'native/d3d9_vk_shaders.inc',pc/'native/d3d9_uniform_state.h',pc/'native/d3d9_uniform_pack.h',pc/'native/d3d9_vk_textures.inc',pc/'native/d3d9_texture_upload.h',pc/'native/d3d9_vk_backend.h',pc/'native/host_mutex.h',pc/'tests/vk_backend_fault_proxy.h',pc/'tests/vk_backend_buffers_fixture.h',pc/'tests/vk_backend_textures_fixture.h',pc/'tests/vk_backend_shaders_fixture.h',pc/'tests/vk_backend_draw_fixture.h',pc/'tests/vk_backend_render_states_fixture.h',pc/'tests/vk_backend_factory_fixture.h',
           Path(os.environ['SystemRoot'])/'System32/vulkan-1.dll',
           *sorted((headers/'vulkan').glob('*.h')),*sorted((headers/'vk_video').glob('*.h'))]
    test(args,host_source='d3d9_vk_backend_host.c',slots=SLOTS+[(28,9),(29,9),(12,2),(37,3),(38,3),(39,2),(40,2),(43,7),(10,1),(47,2),(48,2),
        (75,2),(76,2),(57,3),(58,3),(41,1),(42,1),(26,7),(27,7),(11,5),(12,1),(100,5),(104,2),(102,3),(101,5),(105,2),(23,9),(19,5),(20,2),(65,3),(64,3),(13,1),(17,3),(92,2),(93,2),(107,2),(108,2),(86,3),(87,2),(88,2),(4,3),(82,7),(81,4),(69,4),(68,4),(None,1),(4,1),(6,3),(7,5),(8,3),(9,6),(10,7),(14,4),(16,7),(6,2),(9,2),(7,2),(15,2),(3,2),(11,7),(12,6)],modes=('normal',),strict_fault_cases=0,extra_inputs=extra,extra_args=(linked/'normal.vs.spv',linked/'normal.ps.spv'),
        extra_sources=(pc/'native/d3d9_vk_backend.c',pc/'native/d3d9_texture_upload.c',pc/'native/d3d9_uniform_state.c',pc/'native/d3d9_uniform_pack.c',pc/'native/d3d9_geometry.c',pc/'native/d3d9_vk_state.c'),extra_includes=(headers,),baseline_lifetime_checks=False,factory_import=True)
    report=json.loads((args.output/'validation.json').read_text());assert report['native_vulkan_backend'] and report['desktop_d3d9_used'] is False
    assert report['pixel_checks']==2304 and report['submissions']==report['completions'] and report['frames_in_flight']==2
    assert report['fault_scenarios']==9 and report['unknown_completion_quarantined'] and report['all_gpu_handles_released'] and report['noncoherent_invalidation_checked']
    assert report['factory_import_bound'] and report['prepared_create_device'] and report['factory_checks']>=57 and report['caps_complete'] is False
    assert report['draws']==26 and report['render_state_pixel_checks']==20480
    assert report['draw_submit_faults']==3
    assert report['gpu_draw_executed'] and report['draw_pixel_checks']==6144 and report['draw_build_faults']==9
    assert report['scaled_vertex_formats_checked']==3
    assert report['native_shader_modules'] and report['shader_module_faults']==2 and report['uniform_bytes_checked']==96 and report['declaration_owned_copy']
    assert report['texture_submit_faults']==3 and report['texture_upload_faults']==5 and report['texture_bytes_checked']>=500 and report['immutable_texture_versions']
    assert report['buffer_upload_faults']==3 and report['immutable_buffer_versions'] and report['buffer_bytes_checked']>=800
    controls={}
    for mode,expected in (('wrong-clamp','native draw pixel'),('wrong-cull','state draw pixel'),('wrong-blend','state draw pixel')):
        negative=subprocess.run([str((args.output/'com-test.exe').resolve()),str((args.output/'fixture.exe').resolve()),str((args.output/'catalog.bin').resolve()),mode,str(linked/'normal.vs.spv'),str(linked/'normal.ps.spv')],capture_output=True,text=True,timeout=60)
        (args.output/(mode+'.log')).write_text(negative.stdout+negative.stderr)
        assert negative.returncode!=0 and expected in negative.stderr
        controls[mode]=True
    report['negative_controls']=controls
    report['wrong_clamp_detected']=True
    (args.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('sdk','python-deps','headers','shaders','linked-shaders','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--cc',nargs='+',required=True);run(p.parse_args())

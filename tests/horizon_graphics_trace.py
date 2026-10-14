"""SDK diagnostics must preserve commands, registers and target state."""
import argparse
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from prepare_horizon_graphics_trace import prepare,remove_trace
p=argparse.ArgumentParser()
p.add_argument('--sdk',type=Path,required=True)
args=p.parse_args()
names=['src/graphics/vulkan/command_processor.cpp','include/rex/graphics/vulkan/deferred_command_buffer.h',
       'src/graphics/vulkan/deferred_command_buffer.cpp','src/graphics/vulkan/render_target_cache.cpp']
with tempfile.TemporaryDirectory(prefix='fnv-graphics-trace-') as folder:
    sdk=Path(folder)
    original={n:remove_trace((args.sdk/n).read_text()) for n in names}
    for n,t in original.items():
        (sdk/n).parent.mkdir(parents=True,exist_ok=True)
        (sdk/n).write_bytes(t.encode())
    prepare(sdk)
    first={n:(sdk/n).read_bytes() for n in names}
    for n in names: assert remove_trace((sdk/n).read_text())==original[n],n
    prepare(sdk)
    assert first=={n:(sdk/n).read_bytes() for n in names}
    assert (sdk/names[0]).read_text().count('fnv_gpu_probe_.Boundary(')==12
    assert 'RexFnvGraphicsBegin(false' in (sdk/names[1]).read_text()
    assert 'RexFnvGraphicsBegin(true' in (sdk/names[2]).read_text()
    assert 'RexFnvGraphicsBarrier(src_stage_mask, dst_stage_mask' in (sdk/names[2]).read_text()
    assert 'RexFnvGraphicsImage(&image_create_info' in (sdk/names[3]).read_text()
    assert 'RexFnvGraphicsResolve(GetPath()' in (sdk/names[3]).read_text()
    assert 'pixel_shader && pixel_shader->writes_depth()' in (sdk/names[0]).read_text()
    assert 'RexFnvGraphicsDepthExport(' in (sdk/names[0]).read_text()
print('PASS bounded trace installation/idempotence; original registers/commands/barriers preserved; 12 boundaries')

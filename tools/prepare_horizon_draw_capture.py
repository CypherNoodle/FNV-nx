"""Install bounded, opt-in geometry diagnostics in the pinned Horizon SDK.

No guest code or draw semantics are changed. Captures contain private game data.
"""
import argparse
import hashlib
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', type=Path, required=True)
args = parser.parse_args()
file = args.sdk / 'src/graphics/vulkan/command_processor.cpp'
original = file.read_bytes()
text = original.decode()
includes = '''#include <cstdio>
#include <filesystem>
#if REX_PLATFORM_SWITCH
extern "C" void* RexGmShadowFor(uint64_t window_address);
#endif
REXCVAR_DEFINE_BOOL(fnv_capture_draws, false, "GPU/Diagnostics",
                    "Capture bounded private CPU geometry snapshots for native renderer research");
'''
namespace_anchor = 'namespace rex::graphics::vulkan {\n\nnamespace {\n'
namespace_addition = '\n#include "fnv_draw_capture.inc"\n'
draw_anchor = '  // Draw.\n#if REX_PLATFORM_SWITCH\n'
draw_addition = '''  // FNV-nx: diagnostic snapshots only; failures never suppress a game draw.
#if REX_PLATFORM_SWITCH
  FnvCaptureDraw(regs, *vertex_shader, pixel_shader, vertex_shader_modification.value,
                 pixel_shader_modification.value, primitive_processing_result,
                 system_constants_, dynamic_viewport_, dynamic_scissor_, memory_,
                 memexport_writes_possible);
#endif

'''
assert text.count(namespace_anchor) == 1, 'Unexpected SDK namespace layout'
assert text.count(draw_anchor) == 1, 'Unexpected SDK draw layout'
if includes not in text:
    text = text.replace(namespace_anchor, includes + '\n' + namespace_anchor)
textures_define = '''REXCVAR_DEFINE_BOOL(fnv_capture_textures, false, "GPU/Diagnostics",
                    "Capture bounded private CPU texture data and descriptors for native RHI research");
'''
if textures_define not in text:
    text = text.replace(namespace_anchor, textures_define + '\n' + namespace_anchor)
if namespace_addition not in text:
    text = text.replace(namespace_anchor, namespace_anchor + namespace_addition)
if draw_addition not in text:
    text = text.replace(draw_anchor, draw_addition + draw_anchor)
payload = Path(__file__).with_name('horizon_draw_capture.inc').read_bytes()
file.with_name('fnv_draw_capture.inc').write_bytes(payload)
file.with_name('fnv_texture_capture.inc').write_bytes(Path(__file__).with_name('horizon_texture_capture.inc').read_bytes())
file.write_bytes(text.encode())
print('Draw capture prepared; before SHA256:', hashlib.sha256(original).hexdigest())
print('After SHA256:', hashlib.sha256(file.read_bytes()).hexdigest())

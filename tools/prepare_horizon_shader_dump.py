"""Add opt-in SPIR-V capture to the pinned external Horizon SDK.

The SDK already dumps Xenos microcode via dump_shaders, but its Vulkan path
does not write translated binaries. This small diagnostic change uses its
existing dump API; it changes no shader or draw semantics.
"""
import argparse
import hashlib
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', type=Path, required=True)
args = parser.parse_args()
file = args.sdk / 'src/graphics/vulkan/pipeline_cache.cpp'
original = file.read_bytes()
text = original.decode().replace('\r\n', '\n')
anchor = '  if (translation.GetOrCreateShaderModule() == VK_NULL_HANDLE) {'
addition = '''  // FNV-nx: opt-in translated shader capture for renderer investigation.
  if (!REXCVAR_GET(dump_shaders).empty()) {
    translation.Dump(REXCVAR_GET(dump_shaders), "spirv");
  }
'''
assert text.count(anchor) == 1, 'Unexpected SDK Vulkan translation layout'
if addition not in text:
    file.write_bytes(text.replace(anchor, addition + anchor).encode())
print('SDK shader capture prepared; before SHA256:', hashlib.sha256(original).hexdigest())
print('After SHA256:', hashlib.sha256(file.read_bytes()).hexdigest())

"""Preserve explicit oDepth scale for host float24 depth attachments.

The SDK viewport remaps D24FS8 to half the guest range. Fragment depth exports
bypass that viewport transformation, so they need the same remap themselves.
Full float24 conversion already does this; FSI keeps guest depth unchanged.
"""
import argparse
from pathlib import Path
import re

BEGIN = '// FNV explicit depth begin'
END = '// FNV explicit depth end'
NAME = 'src/graphics/pipeline/shader/spirv_translator_rb.cpp'

def remove_depth(text):
    return re.sub(r'^([ \t]*)'+BEGIN+r'\n.*?^\1'+END+r'\n', '', text, flags=re.M|re.S)

def prepare(sdk):
    path = sdk/NAME
    original = remove_depth(path.read_text())
    anchor = '      builder_->createStore(depth, output_fragment_depth_);\n    }\n'
    assert original.count(anchor) == 1, 'Pinned SDK depth completion changed'
    code = '''    // FNV explicit depth begin
    else if (current_shader().writes_depth()) {
      // Match the host viewport and depth clears. Do not round/truncate here:
      // the optional float24 conversion branch above already remaps once.
      const spv::Id fnv_is_float24 = builder_->createBinOp(
          spv::OpINotEqual, type_bool_,
          builder_->createBinOp(spv::OpBitwiseAnd, type_uint_, main_system_constant_flags_,
                                builder_->makeUintConstant(kSysFlag_DepthFloat24)),
          builder_->makeUintConstant(0));
      const spv::Id fnv_depth_scale = builder_->createTriOp(
          spv::OpSelect, type_float_, fnv_is_float24,
          builder_->makeFloatConstant(0.5f), const_float_1_);
      const spv::Id fnv_depth = builder_->createLoad(output_fragment_depth_, spv::NoPrecision);
      builder_->createStore(
          builder_->createNoContractionBinOp(spv::OpFMul, type_float_, fnv_depth, fnv_depth_scale),
          output_fragment_depth_);
    }
    // FNV explicit depth end
'''
    text = original.replace(anchor, anchor+code)
    assert remove_depth(text) == original
    if path.read_bytes() != text.encode(): path.write_bytes(text.encode())

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, required=True)
    prepare(p.parse_args().sdk)
    print('PASS explicit FBO depth remap; FSI and float24 conversion branch preserved')

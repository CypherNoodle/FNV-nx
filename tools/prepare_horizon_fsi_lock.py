"""Opt-in shorter interlock scope for guarded opaque FSI shaders."""
import argparse
from pathlib import Path
import re

BEGIN = '// FNV late FSI begin'
END = '// FNV late FSI end'
def remove_late_fsi(text):
    return re.sub(r'^([ \t]*)' + BEGIN + r'\n.*?^\1' + END + r'\n',
                  '', text, flags=re.MULTILINE | re.DOTALL)

def prepare(sdk):
    names = ['include/rex/graphics/pipeline/shader/spirv_translator.h',
             'src/graphics/vulkan/command_processor.cpp',
             'src/graphics/pipeline/shader/spirv_translator_rb.cpp']
    before = {n:remove_late_fsi((sdk/n).read_text()) for n in names}
    text = dict(before)
    def add(name, anchor, code, before_anchor=False, indent=''):
        assert text[name].count(anchor) == 1, (name,anchor)
        block = indent + BEGIN + '\n' + code.rstrip() + '\n' + indent + END + '\n'
        text[name] = text[name].replace(anchor, block+anchor if before_anchor else anchor+block, 1)
    header, cp, rb = names
    # New enum meaning is part of shader storage identity. Preserve the original
    # statement in the excluded branch so the preparation can round-trip.
    add(header, '    static constexpr uint32_t kVersion = 12;\n',
        '    #if 1\n    static constexpr uint32_t kVersion = 13;\n    #else',
        before_anchor=True, indent='    ')
    add(header, '    static constexpr uint32_t kVersion = 12;\n', '    #endif', indent='    ')
    add(header, '      kFloat24Rounding,\n',
        '      kFsiLateOpaque, // guarded side-effect-free FSI shader; keep depth/blend inside interlock',
        indent='      ')
    add(header, '    assert_true(edram_fragment_shader_interlock_);\n',
        '    if (GetSpirvShaderModification().pixel.depth_stencil_mode ==\n'
        '            Modification::DepthStencilMode::kFsiLateOpaque &&\n'
        '        current_shader().implicit_early_z_write_allowed()) return false;',
        indent='    ')
    add(cp, '#include <rex/graphics/vulkan/command_processor.h>\n',
        '#if REX_PLATFORM_SWITCH\n#include "../fnv_fsi_lock_policy.h"\n'
        'extern "C" bool RexFnvLateFsiEnabled();\n'
        'extern "C" void RexFnvLateFsiNote(bool selected);\n#endif')
    add(cp, '                                             : SpirvShaderTranslator::Modification(0);\n', '''
    #if REX_PLATFORM_SWITCH
    const bool fnv_late_fsi = fnv::LateFsiEligible(
        RexFnvLateFsiEnabled(),
        render_target_cache_->GetPath() == RenderTargetCache::Path::kPixelShaderInterlock,
        pixel_shader != nullptr,
        pixel_shader ? pixel_shader->kills_pixels() : true,
        pixel_shader ? pixel_shader->writes_depth() : true,
        pixel_shader ? bool(pixel_shader->memexport_eM_written()) : true,
        bool(vertex_shader->memexport_eM_written()),
        normalized_depth_control.stencil_enable,
        draw_util::DoesCoverageDependOnAlpha(regs.Get<reg::RB_COLORCONTROL>()));
    if (fnv_late_fsi)
      pixel_shader_modification.pixel.depth_stencil_mode =
          SpirvShaderTranslator::Modification::DepthStencilMode::kFsiLateOpaque;
    RexFnvLateFsiNote(fnv_late_fsi);
    #endif''', indent='    ')
    # Deferred kill updates the mask in conditional branches. Merge the SSA
    # values before another branch consumes them; never edit emitted SPIR-V.
    add(rb, '    SpirvBuilder::IfBuilder if_alpha_test_function_is_non_always(\n',
        '    const spv::Id fnv_mask_before_alpha_test = fsi_sample_mask_in_rt_0_alpha_tests;',
        before_anchor=True, indent='    ')
    add(rb, '    if_alpha_test_function_is_non_always.makeEndIf();\n',
        '    if (edram_fragment_shader_interlock_ && !features_.demote_to_helper_invocation)\n'
        '      fsi_sample_mask_in_rt_0_alpha_tests =\n'
        '          if_alpha_test_function_is_non_always.createMergePhi(\n'
        '              fsi_sample_mask_in_rt_0_alpha_tests, fnv_mask_before_alpha_test);', indent='    ')
    add(rb, '      SpirvBuilder::IfBuilder if_alpha_to_coverage_enabled(\n',
        '      const spv::Id fnv_mask_before_alpha_coverage = fsi_sample_mask_in_rt_0_alpha_tests;',
        before_anchor=True, indent='      ')
    add(rb, '      if_alpha_to_coverage_enabled.makeEndIf();\n',
        '      if (edram_fragment_shader_interlock_ && !features_.demote_to_helper_invocation)\n'
        '        fsi_sample_mask_in_rt_0_alpha_tests =\n'
        '            if_alpha_to_coverage_enabled.createMergePhi(\n'
        '                fsi_sample_mask_in_rt_0_alpha_tests, fnv_mask_before_alpha_coverage);', indent='      ')
    for name,value in text.items():
        assert remove_late_fsi(value) == before[name], ('Original statements changed',name)
        if (sdk/name).read_bytes() != value.encode(): (sdk/name).write_bytes(value.encode())
    source = Path(__file__).resolve().parents[1]/'src/fsi_lock_policy.h'
    destination = sdk/'src/graphics/fnv_fsi_lock_policy.h'
    if not destination.exists() or destination.read_bytes() != source.read_bytes():
        destination.write_bytes(source.read_bytes())

if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk',type=Path,required=True)
    prepare(p.parse_args().sdk)
    print('PASS FSI late-opaque preparation: cache version13, guarded variant4, original statements preserved')

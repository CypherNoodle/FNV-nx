"""Verify SDK roundtrips, guarded modifier selection and alias mapping source."""
import argparse
from pathlib import Path
import re
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from prepare_horizon_fsi_lock import prepare, remove_late_fsi

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk',type=Path,required=True)
args=p.parse_args()
names=['include/rex/graphics/pipeline/shader/spirv_translator.h',
       'src/graphics/vulkan/command_processor.cpp',
       'src/graphics/pipeline/shader/spirv_translator_rb.cpp']
with tempfile.TemporaryDirectory(prefix='fnv-fsi-lock-test-') as folder:
    sdk=Path(folder)
    baseline={}
    for name in names:
        baseline[name]=remove_late_fsi((args.sdk/name).read_text())
        path=sdk/name
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(baseline[name])
    prepare(sdk)
    first={n:(sdk/n).read_bytes() for n in names}
    for n in names: assert remove_late_fsi((sdk/n).read_text())==baseline[n]
    prepare(sdk)
    assert first=={n:(sdk/n).read_bytes() for n in names}
    header=(sdk/names[0]).read_text()
    assert 'kVersion = 13;' in header and 'kFsiLateOpaque' in header
    assert 'current_shader().implicit_early_z_write_allowed()) return false;' in header
    cpp=(sdk/names[1]).read_text()
    for guard in ['pixel_shader->kills_pixels()', 'pixel_shader->writes_depth()',
                  'pixel_shader->memexport_eM_written()', 'vertex_shader->memexport_eM_written()',
                  'normalized_depth_control.stencil_enable', 'draw_util::DoesCoverageDependOnAlpha']:
        assert guard in cpp,guard
    rb=(sdk/names[2]).read_text()
    assert 'fnv_mask_before_alpha_test' in rb and 'fnv_mask_before_alpha_coverage' in rb
    assert 'if_alpha_test_function_is_non_always.createMergePhi(' in rb
    assert 'if_alpha_to_coverage_enabled.createMergePhi(' in rb
memory=(args.sdk/'src/system/xmemory.cpp').read_text()
match=re.search(r'uint32_t PhysicalHeap::GetPhysicalAddress\(uint32_t address\) const \{(.*?)\n\}',memory,re.S)
assert match and 'address -= heap_base_;' in match[1]
assert 'if (heap_base_ >= 0xE0000000)' in match[1] and 'address += 0x1000;' in match[1]
assert '0x1FD00000, 4096, &heaps_.physical' in memory
print('PASS FSI guarded variant/source preservation/idempotence; shader identity13; E alias +0x1000 source verified')

"""Preparation roundtrip and branch guards; emitted shader tests are separate."""
import argparse
from pathlib import Path
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from prepare_horizon_fbo_depth import NAME, prepare, remove_depth
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk', type=Path, required=True)
args=p.parse_args()
with tempfile.TemporaryDirectory(prefix='fnv-depth-') as folder:
    sdk=Path(folder)
    original=remove_depth((args.sdk/NAME).read_text())
    path=sdk/NAME
    path.parent.mkdir(parents=True)
    path.write_bytes(original.encode())
    prepare(sdk)
    first=path.read_bytes()
    assert remove_depth(path.read_text()) == original
    prepare(sdk)
    assert path.read_bytes() == first
    text=path.read_text()
    scope=text[text.index('  if (!edram_fragment_shader_interlock_ && output_fragment_depth_'):]
    assert scope.index('kFloat24Rounding') < scope.index('else if (current_shader().writes_depth())')
    assert 'spv::OpSelect, type_float_, fnv_is_float24,' in scope
    assert 'builder_->makeFloatConstant(0.5f), const_float_1_' in scope
print('PASS FBO depth preparation idempotence/original preservation/branch guards')

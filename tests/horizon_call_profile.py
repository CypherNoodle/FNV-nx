"""Check timer-only SDK round trips, idempotence and startup report exclusion."""
import argparse
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_horizon_call_profile import prepare, remove_timers
from analyze_switch_profile import analyze

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk', type=Path, required=True)
args = p.parse_args()
names = ['src/graphics/vulkan/command_processor.cpp', 'src/graphics/vulkan/texture_cache.cpp',
         'src/graphics/vulkan/pipeline_cache.cpp', 'src/graphics/vulkan/render_target_cache.cpp',
         'src/graphics/command_processor.cpp', 'src/graphics/shared_memory.cpp', 'src/graphics/primitive_processor.cpp',
         'src/graphics/graphics_system.cpp', 'src/core/guest_memory_switch.cpp', 'src/ui/switch_perf.cpp',
         'src/kernel/xboxkrnl/xboxkrnl_io.cpp']
with tempfile.TemporaryDirectory(prefix='fnv-profile-test-') as folder:
    sdk = Path(folder)
    before = {}
    for name in names:
        before[name] = remove_timers((args.sdk / name).read_text())
        assert 'FNV_CALL_SCOPE' not in before[name]
        path = sdk / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(before[name])
    assert prepare(sdk) == 21
    after = {name: (sdk / name).read_bytes() for name in names}
    for name in names:
        assert remove_timers((sdk / name).read_text()) == before[name], name
    assert prepare(sdk) == 0
    assert after == {name: (sdk / name).read_bytes() for name in names}
sample = '''==== 0.0 s | mode: startup | game 0.0 fps | CPU total 0%
waited 123 (999999999.0 ms)
==== 10.0 s | mode: normal | game 12.0 fps | CPU total 80%
waited 90 (900.0 ms)
       fence-block: 20 calls | 0.17 calls/frame | 5.000 ms/frame | 60.000 ms/s | mean 30.0000 ms | max 50.000 ms
'''
result = analyze(sample)
assert len(result['intervals']) == 1
row = result['intervals'][0]
assert row['fence_wait_ms_per_second'] == 900.0
assert row['call_timings'][0]['name'] == 'fence-block'
assert row['call_timings'][0]['ms_per_frame'] == 5.0
print('PASS: 21 function scopes preserve original statements; idempotent; startup excluded; timings parsed')

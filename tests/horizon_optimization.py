"""Check source preservation, idempotence and nonblocking GPU resource policy."""
import argparse
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from prepare_horizon_ring_progress import prepare as prepare_ring, remove_notifications
from prepare_horizon_optimization import prepare, remove_extensions

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--sdk', type=Path, required=True)
args = p.parse_args()
names = ['src/graphics/command_processor.cpp', 'src/graphics/vulkan/command_processor.cpp',
         'include/rex/graphics/vulkan/command_processor.h',
         'include/rex/graphics/vulkan/deferred_command_buffer.h',
         'src/graphics/vulkan/deferred_command_buffer.cpp',
         'src/ui/vulkan/vulkan_presenter.cpp', 'src/ui/switch_perf.cpp']
def original(text): return remove_extensions(remove_notifications(text))
with tempfile.TemporaryDirectory(prefix='fnv-optimization-test-') as folder:
    sdk = Path(folder)
    baseline = {}
    for name in names:
        path = sdk / name
        path.parent.mkdir(parents=True, exist_ok=True)
        baseline[name] = original((args.sdk / name).read_text())
        path.write_text(baseline[name])
    prepare_ring(sdk)
    prepare(sdk)
    first = {name: (sdk / name).read_bytes() for name in names}
    for name in names: assert original((sdk / name).read_text()) == baseline[name], name
    prepare_ring(sdk)
    prepare(sdk)
    assert first == {name: (sdk / name).read_bytes() for name in names}
    cp = (sdk / 'src/graphics/command_processor.cpp').read_text()
    assert cp.count('RexFnvRingProgressPublish(read_ptr_writeback_ptr_,') == 2
    assert cp.count('RexFnvRingProgressInvalidate();') == 4
    vk = (sdk / 'src/graphics/vulkan/command_processor.cpp').read_text()
    assert 'if (RexFnvBatchSubmissionsEnabled() && submission_open_) EndSubmission(false);' in vk
    assert vk.index('AwaitAllQueueOperationsCompletion();', vk.index('::ShutdownContext()')) < vk.index('fnv_gpu_probe_.Shutdown()')
    assert vk.index('dfn.vkQueueSubmit(', vk.index('::EndSubmission(')) < vk.index('fnv_gpu_probe_.Commit(')
    probe = (sdk / 'include/rex/graphics/vulkan/fnv_gpu_timestamps.h').read_text()
    assert 'VK_QUERY_RESULT_WAIT_BIT' not in probe and 'vkWaitForFences' not in probe
    assert 's.submission > completed' in probe and 'samples_[slot].pending' in probe
    assert '(++sequence_ % 16)' in probe and 's.count >= kQueries - 1' in probe
print('PASS SDK preservation/idempotence; notification-after-store; batch wait flush; completed-only queries; bounded slots/markers')

"""Add opt-in inclusive host timers without changing waits, barriers or calls."""
import argparse
from pathlib import Path
import re


def remove_timers(text):
    text = re.sub(r'#include "(?:\.\./)*(?:graphics/)?fnv_call_profile.h"\n', '', text)
    text = re.sub(r'^  FNV_CALL_SCOPE\(\d+\);\n', '', text, flags=re.MULTILINE)
    for id, statement in (
        (13, '    CheckSubmissionFenceAndDeviceLoss(sampler_overflow_await_submission);'),
        (18, '        rex::thread::Wait(write_ptr_index_event_.get(), true, std::chrono::milliseconds(5));'),
    ):
        indent = statement[:len(statement) - len(statement.lstrip())]
        text = text.replace(indent + f'{{ FNV_CALL_SCOPE({id});\n' + statement + '\n' + indent + '}', statement)
    text = text.replace('    VkResult wait_result;\n    {\n      FNV_CALL_SCOPE(4);\n      wait_result =',
                        '    VkResult wait_result =')
    text = text.replace('submissions_in_flight_fences_.data(), VK_TRUE, UINT64_MAX);\n    }',
                        'submissions_in_flight_fences_.data(), VK_TRUE, UINT64_MAX);')
    text = text.replace('    VkResult fence_status;\n    {\n      FNV_CALL_SCOPE(5);\n      fence_status =',
                        '    VkResult fence_status =')
    text = text.replace('submissions_in_flight_fences_[fences_awaited], VK_TRUE, 0);\n    }',
                        'submissions_in_flight_fences_[fences_awaited], VK_TRUE, 0);')
    text = text.replace('  RexFnvCallProfileReport(f, seconds, frames);\n', '')
    return text.replace('    RexFnvThreadCoreReport(f, s.handle.load(), elapsed_ticks);\n', '')


def prepare(sdk):
    header = Path(__file__).resolve().parents[1] / 'src/switch_call_profile.h'
    destination = sdk / 'src/graphics/fnv_call_profile.h'
    if not destination.exists() or destination.read_bytes() != header.read_bytes():
        destination.write_bytes(header.read_bytes())
    specs = {
        'src/graphics/vulkan/command_processor.cpp': {
            'VulkanCommandProcessor::IssueDraw': 0,
            'VulkanCommandProcessor::IssueCopy': 1,
            'VulkanCommandProcessor::EndSubmission': 2,
            'VulkanCommandProcessor::BeginSubmission': 3,
            'VulkanCommandProcessor::IssueDraw_MemexportReadbackFullPath': 14,
            'VulkanCommandProcessor::IssueDraw_MemexportReadbackFastPath': 15,
            'VulkanCommandProcessor::IssueCopy_ReadbackResolvePath': 16,
            'VulkanCommandProcessor::IssueSwap': 22,
        },
        'src/graphics/vulkan/texture_cache.cpp': {'VulkanTextureCache::RequestTextures': 6},
        'src/graphics/shared_memory.cpp': {'SharedMemory::RequestRanges': 7},
        'src/graphics/primitive_processor.cpp': {'PrimitiveProcessor::Process': 26},
        'src/graphics/vulkan/pipeline_cache.cpp': {'VulkanPipelineCache::ConfigurePipeline': 8},
        'src/graphics/vulkan/render_target_cache.cpp': {'VulkanRenderTargetCache::Update': 11},
        'src/graphics/command_processor.cpp': {
            'CommandProcessor::ExecutePacketType3_WAIT_REG_MEM': 9,
            'CommandProcessor::ExecutePacketType3_INTERRUPT': 10,
            'CommandProcessor::ExecutePrimaryBuffer': 19,
        },
        'src/graphics/graphics_system.cpp': {'GraphicsSystem::DispatchInterruptCallback': 20,
                                          'GraphicsSystem::MarkVblank': 23},
        'src/core/guest_memory_switch.cpp': {'RexGmProtect': 21},
        'src/kernel/xboxkrnl/xboxkrnl_io.cpp': {'NtReadFile_entry': 24, 'NtWriteFile_entry': 25},
    }
    changes = 0
    for name, functions in specs.items():
        path = sdk / name
        text = path.read_text()
        before = remove_timers(text)
        assert 'FNV_CALL_SCOPE' not in before, name
        include = '#include "fnv_call_profile.h"'
        if '/vulkan/' in name: include = '#include "../fnv_call_profile.h"'
        if '/core/' in name: include = '#include "../graphics/fnv_call_profile.h"'
        if '/kernel/' in name: include = '#include "../../graphics/fnv_call_profile.h"'
        # After the first existing include: leaves platform/header comments intact.
        if include not in text:
            text = re.sub(r'(#include[^\n]+\n)', r'\1' + include + '\n', text, count=1)
        for function, id in functions.items():
            pattern = r'(\b(?:bool|void|uint32_t|u32) ' + re.escape(function) + r'\([^{}]*\) \{\n)'
            matches = list(re.finditer(pattern, text))
            assert len(matches) == 1, (name, function, len(matches))
            marker = f'  FNV_CALL_SCOPE({id});\n'
            m = matches[0]
            if not text[m.end():].startswith(marker):
                text = text[:m.end()] + marker + text[m.end():]
                changes += 1
        if name.endswith('/vulkan/command_processor.cpp'):
            # Time just the blocking driver wait, separately from nonblocking queries.
            anchor = '    VkResult wait_result =\n        dfn.vkWaitForFences'
            if anchor in text:
                text = text.replace(anchor, '    VkResult wait_result;\n    {\n      FNV_CALL_SCOPE(4);\n      wait_result =\n        dfn.vkWaitForFences', 1)
                end = 'submissions_in_flight_fences_.data(), VK_TRUE, UINT64_MAX);'
                assert text.count(end) == 1
                text = text.replace(end, end + '\n    }', 1)
            anchor = '    VkResult fence_status =\n        dfn.vkWaitForFences'
            if anchor in text:
                text = text.replace(anchor, '    VkResult fence_status;\n    {\n      FNV_CALL_SCOPE(5);\n      fence_status =\n        dfn.vkWaitForFences', 1)
                end = 'submissions_in_flight_fences_[fences_awaited], VK_TRUE, 0);'
                assert text.count(end) == 1
                text = text.replace(end, end + '\n    }', 1)
            anchor = '    CheckSubmissionFenceAndDeviceLoss(sampler_overflow_await_submission);'
            marker = '    { FNV_CALL_SCOPE(13);\n' + anchor + '\n    }'
            if marker not in text:
                assert text.count(anchor) == 1
                text = text.replace(anchor, marker)
        if name == 'src/graphics/command_processor.cpp':
            anchor = '        rex::thread::Wait(write_ptr_index_event_.get(), true, std::chrono::milliseconds(5));'
            marker = '        { FNV_CALL_SCOPE(18);\n' + anchor + '\n        }'
            if marker not in text:
                assert text.count(anchor) == 1
                text = text.replace(anchor, marker)
        assert remove_timers(text) == before, ('Original statements changed', name)
        if text != path.read_text(): path.write_text(text)
    path = sdk / 'src/ui/switch_perf.cpp'
    text = path.read_text()
    before = remove_timers(text)
    include = '#include "../graphics/fnv_call_profile.h"'
    if include not in text:
        text = text.replace('#include <switch.h>', '#include <switch.h>\n' + include, 1)
    anchor = '  const double frames = delta(0);'
    report = '  RexFnvCallProfileReport(f, seconds, frames);'
    if report not in text:
        assert text.count(anchor) == 1
        text = text.replace(anchor, anchor + '\n' + report, 1)
    core_report = '    RexFnvThreadCoreReport(f, s.handle.load(), elapsed_ticks);'
    if core_report not in text:
        anchor = '    auto lo = std::lower_bound(samples.begin(), samples.end(), i,'
        assert text.count(anchor) == 1
        text = text.replace(anchor, core_report + '\n\n' + anchor, 1)
        # Preserve the original blank line count when removing the insertion.
        text = text.replace(core_report + '\n\n', core_report + '\n', 1)
    assert remove_timers(text) == before, 'Original profiler statements changed'
    if text != path.read_text(): path.write_text(text)
    return changes


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    args = parser.parse_args()
    print('PASS call timing instrumentation; newly inserted function scopes:', prepare(args.sdk))

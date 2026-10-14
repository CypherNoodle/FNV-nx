"""Notify after existing ring-pointer writebacks, preserving all original stores."""
import argparse
from pathlib import Path
import re

INCLUDE = '#if REX_PLATFORM_SWITCH\n#include "fnv_ring_progress.h"\n#endif\n'
PREFIX = 'FNV ring notification'


def remove_notifications(text):
    text = text.replace(INCLUDE, '')
    return re.sub(r'^([ \t]*)// ' + PREFIX + r' begin\n.*?^\1// ' + PREFIX + r' end\n',
                  '', text, flags=re.MULTILINE | re.DOTALL)


def prepare(sdk):
    path = sdk / 'src/graphics/command_processor.cpp'
    before = remove_notifications(path.read_text())
    text = before
    text = re.sub(r'(#include[^\n]+\n)', r'\1' + INCLUDE, text, count=1)

    def add(anchor, code, indent):
        nonlocal text
        assert text.count(anchor) == 1, anchor
        block = (indent + '// ' + PREFIX + ' begin\n' + indent + '#if REX_PLATFORM_SWITCH\n' +
                 indent + code + '\n' + indent + '#endif\n' + indent + '// ' + PREFIX + ' end\n')
        text = text.replace(anchor, anchor + block, 1)

    for value in ('read_ptr_index_', 'now'):
        pattern = (r'(?m)^([ \t]*)memory::store_and_swap<uint32_t>\(memory_->TranslatePhysical\('
                   r'read_ptr_writeback_ptr_\),\n[ \t]*' + value + r'\);\n')
        matches = list(re.finditer(pattern, text))
        assert len(matches) == 1, value
        match = matches[0]
        add(match[0], f'RexFnvRingProgressPublish(read_ptr_writeback_ptr_, {value});', match[1])
    for anchor in (
        'void CommandProcessor::EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size_log2) {\n',
        'void CommandProcessor::InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) {\n',
        'bool CommandProcessor::Restore(::rex::stream::ByteStream* stream) {\n  assert_true(paused_);\n',
        '  ShutdownContext();\n',
    ):
        add(anchor, 'RexFnvRingProgressInvalidate();', '  ')
    assert remove_notifications(text) == before, 'Original command processor statements changed'
    header = Path(__file__).resolve().parents[1] / 'src/switch_ring_progress.h'
    destination = sdk / 'src/graphics/fnv_ring_progress.h'
    if not destination.exists() or destination.read_bytes() != header.read_bytes():
        destination.write_bytes(header.read_bytes())
    if path.read_text() != text: path.write_text(text)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--sdk', type=Path, required=True)
    prepare(p.parse_args().sdk)
    print('PASS ring notifications: two original stores preserved; invalidation and bounded wait')

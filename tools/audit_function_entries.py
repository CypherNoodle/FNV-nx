"""Inventory data-referenced code entries and audit bounded original leaf functions.

An unregistered address is a candidate, not proof of a missing function:
exception tables also reference internal labels. No game bytes are exported.
"""
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import re
import struct
from audit_adjustors import IMAGE_SHA256

LEAF_SIZES = {
    0x8282C8E0: 24, 0x82C17A70: 12, 0x82CA7E70: 16,
    0x82CA7FD0: 40, 0x82CA7FF8: 40, 0x82CA8020: 40,
    0x82CA8048: 40, 0x82CA8460: 32, 0x82D1BD88: 44,
    0x82E42900: 80, 0x82E7C880: 44,
    # Four bounded clusters of virtual forwarders, including the computed
    # runtime target 82814F90 (no literal data pointer). Each cluster ends at
    # a registered entry and follows a return or another tail forwarder.
    0x8224EA50: 16, 0x8224EA60: 16, 0x82341AE8: 16,
    0x8261F0D0: 16, 0x8261F0E0: 16, 0x82814F90: 16,
    # Computed dispatch adapters: independently terminated, ending at 82A83300.
    0x82A83238: 40, 0x82A83260: 40,
    0x82A83288: 24, 0x82A832A0: 24, 0x82A832B8: 24,
    0x82A832D0: 24, 0x82A832E8: 24,
}
ADAPTER_FORWARDERS = {0x82A83238, 0x82A83260, 0x82A83288, 0x82A832A0,
                      0x82A832B8, 0x82A832D0, 0x82A832E8}
FORWARDER_CLUSTERS = ((0x8224EA50, 0x8224EA70), (0x82341AE8, 0x82341AF8),
                      (0x8261F0D0, 0x8261F0F0), (0x82814F90, 0x82814FA0))
FORWARDERS = {address for start, end in FORWARDER_CLUSTERS for address in range(start, end, 16)}


def relative_target(pc, word, bits):
    value = word & ((1 << bits) - 4)
    if value & (1 << (bits - 1)):
        value -= 1 << bits
    assert word & 3 == 0, 'Linked or absolute branch requires separate review'
    return (pc + value) & 0xFFFFFFFF


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--generated-dir', type=Path, required=True)
    parser.add_argument('--require-registered', action='store_true')
    args = parser.parse_args()
    image = args.image.read_bytes()
    assert hashlib.sha256(image).hexdigest() == IMAGE_SHA256
    init = (args.generated_dir / 'fnv_xbox_init.cpp').read_text()
    registered = {int(v, 16) for v in re.findall(r'\{ 0x([0-9A-F]{8}),', init)}
    nt = struct.unpack_from('<I', image, 0x3C)[0]
    count = struct.unpack_from('<H', image, nt + 6)[0]
    optional_size = struct.unpack_from('<H', image, nt + 20)[0]
    sections = []
    for i in range(count):
        offset = nt + 24 + optional_size + i * 40
        name = image[offset:offset + 8].split(b'\0')[0].decode()
        size, rva = struct.unpack_from('<II', image, offset + 8)
        flags = struct.unpack_from('<I', image, offset + 36)[0]
        sections.append((name, rva, size, flags))
    code = [(0x82000000 + rva, 0x82000000 + rva + size)
            for name, rva, size, flags in sections if flags & 0x20000000]
    refs = {}
    # Relocations describe offsets, not runtime code pointers; do not treat
    # coincidental values in their payload as function-table evidence.
    for name, rva, size, flags in sections:
        if flags & 0x20000000 or name == '.reloc':
            continue
        for offset in range(rva, min(rva + size, len(image)) - 3, 4):
            value = struct.unpack_from('>I', image, offset)[0]
            if value % 4 == 0 and any(lo <= value < hi for lo, hi in code):
                refs.setdefault(value, []).append({'section': name, 'address': hex(0x82000000 + offset)})
    pdata = next(s for s in sections if s[0] == '.pdata')
    ranges = []
    for offset in range(pdata[1], pdata[1] + pdata[2], 8):
        start, packed = struct.unpack_from('>II', image, offset)
        ranges.append((start, start + max(4, ((packed >> 8) & 0x3FFFFF) * 4)))
    ranges.sort()
    starts = [start for start, end in ranges]
    candidates = []
    for address in sorted(refs.keys() - registered):
        index = bisect.bisect_right(starts, address) - 1
        parent = ranges[index] if index >= 0 and address < ranges[index][1] else None
        candidates.append({'address': hex(address), 'references': refs[address],
                           'classification': 'inside_pdata_range' if parent else 'outside_pdata_requires_review',
                           'pdata_parent': hex(parent[0]) if parent else None})
    direct_calls = 0
    missing_direct_targets = {}
    for lo, hi in code:
        for pc in range(lo, hi - 3, 4):
            word = struct.unpack_from('>I', image, pc - 0x82000000)[0]
            if word & 0xFC000003 != 0x48000001:  # Relative bl encoding
                continue
            direct_calls += 1
            target = relative_target(pc, word & ~1, 26)
            if target not in registered and any(start <= target < end for start, end in code):
                missing_direct_targets.setdefault(hex(target), []).append(hex(pc))
    leaf_entries = []
    known = registered | set(LEAF_SIZES)
    assert 0x82A83220 in registered and 0x82A83300 in registered
    assert struct.unpack_from('>I', image, 0x82A83234 - 0x82000000)[0] == 0x4E800420
    for start, end in FORWARDER_CLUSTERS:
        assert end in registered, 'Forwarder cluster has no registered upper boundary'
        assert struct.unpack_from('>I', image, start - 0x82000000 - 4)[0] in (0x4E800020, 0x4E800420)
    for address, size in LEAF_SIZES.items():
        if address in FORWARDERS:
            first, load, ctr, tail = struct.unpack_from('>IIII', image, address - 0x82000000)
            assert first == 0x81630000 and load >> 16 in (0x814B, 0x816B)
            assert ctr == (0x7D4903A6 if load >> 16 == 0x814B else 0x7D6903A6)
            assert tail == 0x4E800420, 'Not a bounded original virtual forwarder'
        elif address in ADAPTER_FORWARDERS:
            words = struct.unpack_from('>' + 'I' * (size // 4), image, address - 0x82000000)
            assert words[-1] == 0x4E800420 and words[-2] in (0x7D0903A6, 0x7D2903A6, 0x7D4903A6)
            assert struct.unpack_from('>I', image, address - 0x82000000 - 4)[0] == 0x4E800420
            if size == 24:
                slot = 0x24 + ((address - 0x82A83288) // 24) * 4
                assert words[0] == 0x80640000 and words[1] in (0x7CA42B78, 0x7CE63B78)
                assert words[2] == 0x81630000 and words[3] == (0x814B0000 | slot)
            elif address == 0x82A83238:
                assert words[-4] == 0x81430000 and words[-3] == 0x812A0020
            else:
                assert words[0] == 0x81460000 and words[-3] == 0x810A000C
        else:
            assert refs.get(address), f'No original data reference: {address:08X}'
        assert not any(start <= address < end for start, end in ranges), 'Leaf overlaps PDATA'
        assert not any(address < other < address + size for other in known), 'Overlapping entry'
        pending = [address]
        reached = set()
        while pending:
            pc = pending.pop()
            if pc in reached:
                continue
            assert address <= pc < address + size and pc % 4 == 0, 'Invalid leaf fallthrough'
            reached.add(pc)
            word = struct.unpack_from('>I', image, pc - 0x82000000)[0]
            opcode = word >> 26
            if word in (0x4E800020, 0x4E800420):  # blr / bctr
                continue
            if opcode in (16, 18):
                target = relative_target(pc, word, 16 if opcode == 16 else 26)
                if address <= target < address + size:
                    pending.append(target)
                else:
                    assert target in known, f'Unregistered tail target: {target:08X}'
                if opcode == 18:
                    continue
            else:
                assert opcode != 19, 'Other branch-register instruction requires review'
            pending.append(pc + 4)
        assert len(reached) * 4 == size, 'Declared extent includes unreachable words'
        if args.require_registered:
            assert address in registered, f'Leaf not registered: {address:08X}'
        leaf_entries.append({'address': hex(address), 'size': size,
                             'registered': address in registered, 'references': len(refs.get(address, [])),
                             'evidence': 'bounded_dispatch_adapter_cluster' if address in ADAPTER_FORWARDERS else
                             'bounded_virtual_forwarder_cluster' if address in FORWARDERS else 'original_data_pointer'})
    if args.require_registered:
        remaining = set(LEAF_SIZES)
        for file in args.generated_dir.glob('fnv_xbox_recomp.*.cpp'):
            text = file.read_text()
            for address in ADAPTER_FORWARDERS:
                definition = f'DEFINE_REX_FUNC(sub_{address:08X})'
                if definition not in text:
                    continue
                body = text.split(definition, 1)[1].split('\n}', 1)[0]
                assert 'REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);' in body and '\treturn;' in body
                if LEAF_SIZES[address] == 24:
                    slot = 0x24 + ((address - 0x82A83288) // 24) * 4
                    assert 'ctx.r3.u64 = REX_LOAD_U32(ctx.r4.u32 + 0);' in body
                    assert 'ctx.r11.u64 = REX_LOAD_U32(ctx.r3.u32 + 0);' in body
                    assert f'ctx.r10.u64 = REX_LOAD_U32(ctx.r11.u32 + {slot});' in body
                    assert 'ctx.ctr.u64 = ctx.r10.u64;' in body
                    shuffle = 'ctx.r6.u64 = ctx.r7.u64;' if address == 0x82A832A0 else 'ctx.r4.u64 = ctx.r5.u64;'
                    assert shuffle in body
            remaining -= {address for address in remaining if f'DEFINE_REX_FUNC(sub_{address:08X})' in text}
        assert not remaining, 'Missing generated leaf bodies'
    print(json.dumps({'result': 'PASS', 'registered_functions': len(registered),
                      'data_referenced_code_entries': len(refs), 'unregistered_candidates': candidates,
                      'relative_bl_encodings_scanned': direct_calls,
                      'unregistered_relative_bl_code_targets': missing_direct_targets,
                      'leaf_entries': leaf_entries, 'generated_bodies_checked': args.require_registered,
                      'original_image_sha256': IMAGE_SHA256, 'machine_code_modified': False,
                      'coverage': 'Static aligned data pointers and PDATA; computed pointers and exception dispatch require runtime validation'}, indent=2))


if __name__ == '__main__':
    main()

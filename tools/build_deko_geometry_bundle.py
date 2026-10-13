"""Export a bounded private native mesh replay; original pixel stage not used.

Rebases captured physical vertex fetch addresses into a compact SSBO. Raw vertex
and index bytes, endian constants and transform constants remain unchanged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
from inspect_deko_draw_capture import read_capture, sources, repack


def build(records, draw, abi):
    identity = draw['vertex_hash'].upper() + '_' + draw['vertex_modification'].upper()
    if identity not in abi['shader'].upper() or draw['primitive_restart']:
        raise ValueError('Wrong shader variant or unsupported restart')
    if draw['primitive'] not in (4, 6) or abi['native_sampler_bindings']:
        raise ValueError('Requires triangle geometry without vertex textures')
    storage_resources = abi['resources'].get('ssbos', [])
    if len(storage_resources) != 1 or storage_resources[0]['name'] != 'XeSharedMemory':
        raise ValueError('Unsupported vertex storage resources')
    captured = sources(records, 0)
    fetch = bytearray(captured[4])
    storage, rebased = bytearray(), {}
    for (kind, address), value in sorted(records.items()):
        if kind != 6:
            continue
        while len(storage) % 256:
            storage.append(0)
        rebased[address] = (len(storage), len(value))
        storage.extend(value)
    changed = []
    for slot in range(96):
        word, size = struct.unpack_from('<II', fetch, slot * 8)
        address = word & ~3
        if word & 3 != 3 or address not in rebased:
            continue
        target, available = rebased[address]
        if ((size >> 2) & 0xFFFFFF) * 4 > available:
            raise ValueError('Incomplete vertex fetch range')
        struct.pack_into('<I', fetch, slot * 8, target | 3)
        changed.append({'slot': slot, 'original': address, 'native': target})
    if not changed:
        raise ValueError('No captured vertex fetch address matched')
    captured[4] = bytes(fetch)
    system = bytearray(captured[0])
    original_index_address = struct.unpack_from('<I', system, 4)[0]
    if original_index_address != draw['index_base']:
        raise ValueError('Uncaptured system index-loading address')
    while len(storage) % 256:
        storage.append(0)
    struct.pack_into('<I', system, 4, len(storage))
    storage.extend(records[7, draw['index_base']])
    captured[0] = bytes(system)
    if any(block['set'] != 1 or block['binding'] not in captured for block in abi['constant_blocks']):
        raise ValueError('Unsupported native constant binding')
    ubos = {block['binding']: repack(captured[block['binding']], block)
            for block in abi['constant_blocks']}
    if set(ubos) != {0, 1, 3, 4}:
        raise ValueError('Unexpected vertex constants')
    blob = bytearray(128)
    def append(value):
        while len(blob) % 256:
            blob.append(0)
        offset = len(blob)
        blob.extend(value)
        return offset, len(value)
    shared = append(storage)
    index = append(records[7, draw['index_base']])
    blocks = [append(ubos[binding]) for binding in (0, 1, 3, 4)]
    # Deko index reads preserve raw Xbox bytes; the original VS handles swapping.
    header = [0x50524E46, 1, 4 if draw['primitive'] == 4 else 5, draw['count'],
              1 if draw['index_format'] == 0 else 2, 0, 0, shared[1],
              *index, *(number for pair in blocks for number in pair), len(blob), shared[0]]
    assert len(header) == 20
    struct.pack_into('<20I', blob, 0, *header)
    blob[80:104] = records[3, 0]
    blob[104:120] = records[4, 0]
    return bytes(blob), changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--vertex-abi', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    draw = json.loads(args.capture.read_text())
    records = read_capture(args.capture.with_suffix('.bin').read_bytes(), draw)
    abi = json.loads(args.vertex_abi.read_text())
    blob, addresses = build(records, draw, abi)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'geometry.bin').write_bytes(blob)
    shutil.copy2(args.vertex_abi.parent / abi['native_module'], args.output / 'vertex.dksh')
    report = {'draw': draw, 'bytes': len(blob), 'rebased_vertex_fetches': addresses,
              'geometry_sha256': hashlib.sha256(blob).hexdigest(),
              'original_index_vertex_bytes_preserved': True,
              'original_pixel_shader_used': False, 'gpu_verified': False}
    (args.output / 'bundle-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS:', draw['count'], 'indices;', len(blob), 'bundle bytes')


if __name__ == '__main__':
    main()

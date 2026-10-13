"""Validate a private FNVD geometry snapshot and export native constant buffers.

No textures, EDRAM, GPU-written data validation or renderer state conversion.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def read_capture(data, manifest):
    if len(data) > 17 * 1024 * 1024 or data[:8] != struct.pack('<II', 0x44564E46, 1):
        raise ValueError('Invalid FNVD version or capture size')
    records, offset = {}, 8
    while offset < len(data):
        if len(data) - offset < 12:
            raise ValueError('Truncated record header')
        kind, address, size = struct.unpack_from('<III', data, offset)
        offset += 12
        if kind not in range(1, 8) or size > len(data) - offset or not size:
            raise ValueError('Invalid or truncated record')
        key = (kind, address)
        if key in records:
            # Vertex fetch slots may intentionally reference the same range.
            value = data[offset:offset + size]
            old = records[key]
            prefix = min(len(old), len(value))
            if kind != 6 or old[:prefix] != value[:prefix]:
                raise ValueError('Conflicting duplicate memory snapshot')
            if len(value) > len(old):
                records[key] = value
        else:
            records[key] = data[offset:offset + size]
        offset += size
    # SDK SystemConstants also contains the two final tessellation range floats.
    # Captured non-tessellated SPIR-V reflects only the preceding 504 bytes.
    expected = {(1, 0): 0x5003 * 4, (2, 0): 512, (3, 0): 24,
                (4, 0): 16, (5, 0): 32, (5, 1): 32}
    for key, size in expected.items():
        if len(records.get(key, b'')) != size:
            raise ValueError('Missing or malformed state record: ' + str(key))
    if manifest.get('format') != 1 or manifest.get('index_format') not in (0, 1):
        raise ValueError('Unsupported manifest')
    count = manifest.get('count', 0)
    address = manifest.get('index_base', -1)
    if not isinstance(count, int) or not 96 <= count <= 4 * 1024 * 1024:
        raise ValueError('Invalid index count')
    index_size = count * (2 if manifest['index_format'] == 0 else 4)
    if len(records.get((7, address), b'')) != index_size:
        raise ValueError('Index data does not match the draw')
    vertices = [(key, value) for key, value in records.items() if key[0] == 6]
    if not vertices:
        raise ValueError('No vertex memory')
    for (kind, base), value in records.items():
        if kind in (6, 7) and (base < 0 or base + len(value) > 0x20000000):
            raise ValueError('Physical range outside Xenos shared memory')
    return records


def sources(records, stage):
    regs = records[1, 0]
    bitmap = struct.unpack('<4Q', records[5, stage])
    base = 0x4000 if stage == 0 else 0x4400
    packed = b''.join(regs[(base + i * 4) * 4:(base + i * 4 + 4) * 4]
                      for i in range(256) if bitmap[i // 64] & (1 << (i % 64)))
    return {0: records[2, 0], 1 if stage == 0 else 2: packed or bytes(16),
            3: regs[0x4900 * 4:0x4900 * 4 + 160],
            4: regs[0x4800 * 4:0x4800 * 4 + 768]}


def repack(source, block):
    if len(source) < block['source_bytes']:
        raise ValueError('Captured constants do not cover the shader ABI')
    output = bytearray(block['native_bytes'])
    used = set()
    for field in block['fields']:
        count, size = field['elements'], field['element_bytes']
        if count <= 0 or size <= 0:
            raise ValueError('Invalid field copy')
        for i in range(count):
            start = field['source_offset'] + i * field['source_stride']
            target = field['native_offset'] + i * field['native_stride']
            if start < 0 or start + size > len(source) or target < 0 or target + size > len(output):
                raise ValueError('Field copy outside buffer')
            occupied = set(range(target, target + size))
            if used & occupied:
                raise ValueError('Overlapping ABI fields')
            used |= occupied
            output[target:target + size] = source[start:start + size]
    return bytes(output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True, help='Completed .json manifest')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--vertex-abi', type=Path)
    parser.add_argument('--pixel-abi', type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.capture.read_text())
    binary = args.capture.with_suffix('.bin').read_bytes()
    records = read_capture(binary, manifest)
    # Validate all requested exports before writing anything.
    exports = {}
    for stage, path in enumerate((args.vertex_abi, args.pixel_abi)):
        if path is None:
            continue
        abi = json.loads(path.read_text())
        name = 'vertex' if stage == 0 else 'pixel'
        identity = manifest[name + '_hash'].upper() + '_' + manifest[name + '_modification'].upper()
        if identity not in abi['shader'].upper():
            raise ValueError('Shader ABI belongs to a different draw variant')
        captured = sources(records, stage)
        for block in abi['constant_blocks']:
            if block['set'] != 1 or block['binding'] not in captured:
                raise ValueError('Unrecognized constant binding')
            exports[f'{name}-ubo-{block["binding"]}.bin'] = repack(captured[block['binding']], block)
    args.output.mkdir(parents=True, exist_ok=True)
    for name, data in exports.items():
        (args.output / name).write_bytes(data)
    report = {'capture_sha256': hashlib.sha256(binary).hexdigest(), 'validated': True,
              'records': len(records), 'draw': manifest,
              'native_constant_buffers': {name: len(data) for name, data in exports.items()},
              'native_replay_ready': False}
    (args.output / 'capture-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS:', len(records), 'records;', len(exports), 'native constant buffers')


if __name__ == '__main__':
    main()

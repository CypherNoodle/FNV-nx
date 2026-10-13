"""Validate bounded CPU texture sidecars without assuming GPU coherence."""
import argparse
import hashlib
import json
from pathlib import Path


def validate(metadata, read):
    if metadata.get('format') != 1 or metadata.get('cpu_snapshot') is not True:
        raise ValueError('Unsupported texture capture')
    mask = metadata.get('texture_mask', -1)
    if not isinstance(mask, int) or not 0 <= mask <= 0xFFFFFFFF:
        raise ValueError('Invalid texture mask')
    output, slots, captured_bytes = [], set(), 0
    for texture in metadata['textures']:
        slot = texture['slot']
        if not isinstance(slot, int) or not 0 <= slot < 32 or slot in slots or not mask & (1 << slot):
            raise ValueError('Invalid or duplicate texture slot')
        slots.add(slot)
        fetch = texture['fetch']
        if len(fetch) != 6 or any(not isinstance(x, int) or not 0 <= x <= 0xFFFFFFFF for x in fetch):
            raise ValueError('Invalid fetch descriptor')
        if fetch[0] & 3 != 2 or texture['endian'] != (fetch[1] >> 6) & 3:
            raise ValueError('Descriptor type/endian mismatch')
        if texture['dimension'] != (fetch[5] >> 9) & 3 or not 0 <= texture['max_level'] < 14:
            raise ValueError('Invalid dimension/mip range')
        if min(texture['width'], texture['height'], texture['depth']) < 1:
            raise ValueError('Invalid texture dimensions')
        item = {'slot': slot, 'format': texture['format'], 'width': texture['width'],
                'height': texture['height'], 'dimension': texture['dimension'], 'regions': []}
        for prefix, suffix in (('base', 'base'), ('mip', 'mips')):
            size, address, status = (texture[prefix + key] for key in ('_bytes', '_address', '_status'))
            if not isinstance(size, int) or not isinstance(address, int) or size < 0 or address < 0 or address + size > 0x20000000:
                raise ValueError('Physical texture range outside shared memory')
            if status not in (0, 1, -1, -2, -3) or status == 0 and size:
                raise ValueError('Invalid texture status')
            region = {'kind': prefix, 'status': status, 'bytes': size}
            if status == 1:
                if not 0 < size <= 8 * 1024 * 1024:
                    raise ValueError('Captured texture region exceeds its budget')
                data = read(f'fetch-{slot}.{suffix}.bin')
                if len(data) != size:
                    raise ValueError('Truncated or mismatched texture region')
                region['sha256'] = hashlib.sha256(data).hexdigest()
                captured_bytes += size
            item['regions'].append(region)
        output.append(item)
    if captured_bytes > 16 * 1024 * 1024:
        raise ValueError('Draw texture IO budget exceeded')
    return {'result': 'PASS_CPU_CAPTURE', 'textures': output, 'captured_bytes': captured_bytes,
            'skipped': metadata['skipped'], 'gpu_coherence_verified': False,
            'native_texture_upload_verified': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True, help='Texture sidecar capture.json')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    metadata = json.loads(args.capture.read_text())
    report = validate(metadata, lambda name: (args.capture.parent / name).read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS:', len(report['textures']), 'texture descriptors;', report['captured_bytes'], 'bytes')


if __name__ == '__main__':
    main()

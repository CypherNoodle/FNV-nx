"""Read XEX static-library versions without decrypting or exporting code."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect(data):
    if len(data) < 24 or data[:4] != b'XEX2':
        raise ValueError('Not an XEX2 executable')
    count = struct.unpack_from('>I', data, 20)[0]
    if count > (len(data) - 24) // 8:
        raise ValueError('Truncated optional-header table')
    headers = dict(struct.unpack_from('>II', data, 24 + 8 * index) for index in range(count))
    offset = headers.get(0x000200FF)
    if offset is None:
        raise ValueError('XEX does not expose static-library metadata')
    if offset > len(data) - 4:
        raise ValueError('Static-library header outside file')
    size = struct.unpack_from('>I', data, offset)[0]
    if size < 4 or (size - 4) % 16 or size > len(data) - offset:
        raise ValueError('Invalid static-library block size')
    libraries = []
    for cursor in range(offset + 4, offset + size, 16):
        name, major, minor, build, approval, qfe = struct.unpack_from('>8sHHHBB', data, cursor)
        libraries.append({'name': name.rstrip(b'\0').decode('ascii'),
                          'version': f'{major}.{minor}.{build}.{qfe}',
                          'build': build, 'approval': approval})
    return {'format': 'XEX2', 'xex_sha256': hashlib.sha256(data).hexdigest(),
            'libraries': libraries,
            'device_layout_verified': False, 'code_exported': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('xex', type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.xex.read_bytes()), indent=2))


if __name__ == '__main__':
    main()

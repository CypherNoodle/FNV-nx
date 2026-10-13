"""Audit original vtable adjustors against an unmodified local Xbox image.

The report contains addresses and coverage, never game bytes. It checks only
the explicit 8-byte adjustor pattern, not general indirect-call completeness.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

ADDRESSES = (
    0x8286CCC8, 0x82D18460, 0x82D190B0, 0x82D1E918, 0x82D1E920,
    0x82D1E928, 0x82D1E930, 0x82D1E938, 0x82D1E948, 0x82D1E950,
    0x82D250B8, 0x82D250C0, 0x82D250D0, 0x82D250D8, 0x82D250E0,
    0x82D25388, 0x82D25398, 0x82D253A0, 0x82D253A8, 0x82D253B0,
    0x82D3A5D0, 0x82D4D540,
)
IMAGE_SHA256 = 'a57d7fc5b671c35547964b92d7051f375d99bb11cf2b898489b66d616d2b2320'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--generated-dir', type=Path, required=True)
    parser.add_argument('--require-registered', action='store_true')
    args = parser.parse_args()
    image = args.image.read_bytes()
    assert hashlib.sha256(image).hexdigest() == IMAGE_SHA256, 'Unsupported image revision'
    init = (args.generated_dir / 'fnv_xbox_init.cpp').read_text()
    registered = {int(v, 16) for v in re.findall(r'\{ 0x([0-9A-F]{8}),', init)}
    generated_bodies = {}
    if args.require_registered:
        for partition in args.generated_dir.glob('fnv_xbox_recomp.*.cpp'):
            text = partition.read_text()
            for address in ADDRESSES:
                definition = f'DEFINE_REX_FUNC(sub_{address:08X})'
                if definition in text:
                    assert address not in generated_bodies, 'Duplicate adjustor body'
                    generated_bodies[address] = text.split(definition, 1)[1].split('\n}', 1)[0]
    refs = {}
    for offset in range(0, 0x1F0000, 4):
        value = struct.unpack_from('>I', image, offset)[0]
        refs.setdefault(value, []).append(0x82000000 + offset)
    rows = []
    for address in ADDRESSES:
        offset = address - 0x82000000
        adjust, branch = struct.unpack_from('>II', image, offset)
        assert adjust >> 16 == 0x3863, f'Not addi r3,r3: {address:08X}'
        assert branch & 0xFC000003 == 0x48000000, f'Not relative tail branch: {address:08X}'
        displacement = branch & 0x03FFFFFC
        if displacement & 0x02000000:
            displacement -= 0x04000000
        target = (address + 4 + displacement) & 0xFFFFFFFF
        assert target in registered, f'Tail target unregistered: {target:08X}'
        assert refs.get(address), f'No aligned original data reference: {address:08X}'
        if args.require_registered:
            assert address in registered, f'Adjustor unregistered: {address:08X}'
            # Find the real generated body, not just its declaration or table.
            assert address in generated_bodies, f'Missing body: {address:08X}'
            body = generated_bodies[address]
            delta = (adjust & 0xFFFF) - (0x10000 if adjust & 0x8000 else 0)
            assert f'ctx.r3.s64 = ctx.r3.s64 + {delta};' in body
            assert f'sub_{target:08X}(ctx, base);' in body and '\treturn;' in body
        rows.append({'address': f'0x{address:08X}', 'size': 8,
                     'tail_target': f'0x{target:08X}',
                     'data_references': [f'0x{v:08X}' for v in refs[address]],
                     'registered': address in registered})
    print(json.dumps({'result': 'PASS', 'count': len(rows), 'adjustors': rows,
                      'original_image_sha256': IMAGE_SHA256,
                      'generated_bodies_checked': args.require_registered,
                      'machine_code_modified': False,
                      'gameplay_verified': False}, indent=2))


if __name__ == '__main__':
    main()

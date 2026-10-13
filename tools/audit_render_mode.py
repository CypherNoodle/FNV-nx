"""Audit the original renderer bootstrap and propagation into D3D presentation parameters."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from audit_adjustors import IMAGE_SHA256

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--image', type=Path, required=True)
args = p.parse_args()
image = args.image.read_bytes()
assert hashlib.sha256(image).hexdigest() == IMAGE_SHA256
def word(address): return struct.unpack_from('>I', image, address - 0x82000000)[0]
def branch(address):
    value = word(address)
    assert value & 0xFC000003 == 0x48000001
    offset = value & 0x03FFFFFC
    if offset & 0x02000000: offset -= 0x04000000
    return (address + offset) & 0xFFFFFFFF

assert branch(0x8230F3EC) == 0x827B5D98
assert word(0x8230F34C) == 0x8161007C  # widescreen flag, not height
assert word(0x8230F358) == 0x388002D0  # native widescreen height
assert word(0x8230F35C) == 0x38600500  # native widescreen width
assert word(0x8230F374) == 0x90790000  # width global via r25
assert word(0x8230F378) == 0x90980000  # height global via r24
assert word(0x8230F33C) == 0x3B2BF304  # r25 -> 82FFF304
assert word(0x8230F340) == 0x3B0AF308  # r24 -> 82FFF308
assert word(0x827B5DB4) == 0x7C7E1B78  # preserve original width
assert word(0x827B5DB8) == 0x7C9D2378  # preserve original height
assert word(0x827B5E50) == 0x7FA5EB78  # height into initializer r5
assert word(0x827B5E54) == 0x7FC4F378  # width into initializer r4
assert branch(0x827B5E5C) == 0x827AF0F8
assert word(0x827AF108) == 0x7C9D2378
assert word(0x827AF10C) == 0x7CBC2B78
assert word(0x827AF16C) == 0x93BF0000  # presentation parameters width
assert word(0x827AF174) == 0x939F0004  # presentation parameters height
assert word(0x827B62C8) == 0x7FE3FB78  # success returns renderer object
assert word(0x827B5F58) == 0x38600000  # failed creation returns zero
print(json.dumps(dict(result='PASS', image_sha256=IMAGE_SHA256,
    caller_return='0x8230F3F0', original_renderer='0x827B5D98',
    initializer='0x827AF0F8', width_global='0x82FFF304', height_global='0x82FFF308',
    presentation_parameter_offsets=[0, 4], original_called=True,
    dynamic_resolution=False, hardware_dimensions_verified=False), indent=2))

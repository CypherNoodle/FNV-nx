"""Verify the exact original callsite and unchanged-progress polling contract."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from audit_adjustors import IMAGE_SHA256

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--image', type=Path, required=True)
args = parser.parse_args()
image = args.image.read_bytes()
assert hashlib.sha256(image).hexdigest() == IMAGE_SHA256
def word(address): return struct.unpack_from('>I', image, address - 0x82000000)[0]
def branch(address, bits):
    instruction = word(address)
    offset = instruction & ((1 << bits) - 4)
    if offset & (1 << (bits - 1)): offset -= 1 << bits
    return (address + offset) & 0xFFFFFFFF
assert word(0x8224AD2C) & 0xFC000003 == 0x48000001
assert branch(0x8224AD2C, 26) == 0x8278FC10
assert word(0x8224AD30) == 0x2C030000  # compare original return to zero
assert branch(0x8224AD34, 16) == 0x8224AD54  # zero exits
assert branch(0x8224AD50, 16) == 0x8224AD28  # pending progress loops
assert word(0x8278FC18) == 0x9421FF90  # ordinary guest stack frame
assert word(0x8278FC6C) == 0x813F0008  # last observed read-pointer from wait state
assert word(0x8278FC84) == 0x93DF000C  # original timestamp update
assert word(0x8278FC88) == 0x917F0008  # remember newly observed progress
assert word(0x8278FCAC) == 0x817F000C
assert word(0x8278FCB4) == 0x2B0B1388  # original 5000-unit timeout comparison
print(json.dumps({'result': 'PASS', 'caller_return': '0x8224AD30',
                  'original_poll': '0x8278FC10', 'observed_progress_offset': 8,
                  'pending_result': 1, 'sleep_after_unchanged_polls': 32,
                  'requested_sleep_us': 50, 'guest_completion_modified': False,
                  'performance_improvement_verified': False}, indent=2))

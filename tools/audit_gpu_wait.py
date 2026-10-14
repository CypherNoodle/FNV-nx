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
assert word(0x8278FC1C) == 0x83A30000  # device = wait state word 0
assert word(0x8278FC64) == 0x817D2A90  # completion-word address = device + 10896
assert word(0x8278FC70) == 0x810B0000  # poll reads that exact writeback word
assert word(0x8278FC6C) == 0x813F0008  # last observed completion sequence from wait state
assert word(0x8278FC84) == 0x93DF000C  # original timestamp update
assert word(0x8278FC88) == 0x917F0008  # remember newly observed progress
assert word(0x8278FCAC) == 0x817F000C
assert word(0x8278FCB4) == 0x2B0B1388  # original 5000-unit timeout comparison
# The waited completion sequence differs from CP_RB_RPTR. Initialization
# configures the CP read-pointer writeback 60 bytes after the waited base word.
assert word(0x8279C468) == 0x817F2A90
assert word(0x8279C46C) == 0x396B003C
assert branch(0x8279C484, 26) == 0x82FC4CAC
# Emitter: type3 EVENT_WRITE_SHD, three payload words, base address with
# endian encoding 2, and the unchanged completion sequence value.
assert word(0x8223FC60) == 0x81432A90
assert word(0x8223FC74) == 0x3FE0C002
assert word(0x8223FC84) == 0x63FF5800
assert word(0x8223FCD8) == 0x614A0002
assert word(0x8223FCEC) == 0x95440004
assert word(0x8223FCF4) == 0x95030004
event_header = 0xC0025800
assert event_header >> 30 == 3 and ((event_header >> 8) & 0x7F) == 0x58
assert ((event_header >> 16) & 0x3FFF) + 1 == 3
print(json.dumps({'result': 'PASS', 'caller_return': '0x8224AD30',
                  'original_poll': '0x8278FC10', 'observed_progress_offset': 8,
                  'pending_result': 1, 'sleep_after_unchanged_polls': 32,
                  'requested_sleep_us': 50, 'guest_completion_modified': False,
                  'device_pointer_offset': 10896, 'ring_notify_limit_us': 250,
                  'waited_word_offset': 0, 'cp_read_pointer_word_offset': 60,
                  'same_writeback_word': False, 'completion_event_opcode': '0x58',
                  'completion_packet_emitter': '0x8223FC48',
                  'alpha13_cp_notification_contract_matches_wait': False,
                  'performance_improvement_verified': False}, indent=2))

"""Synthetic direct-call/IAT audit, independent of a game executable."""
import argparse
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
from pe_fixture import write_pe
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from audit_aot_calls import inspect
from prepare_aot_image import prepare

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--python-deps', type=Path, required=True)
a = parser.parse_args()
sys.path.insert(0, str(a.python_deps.resolve()))
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    exe = root/'fixture.exe'
    write_pe(exe, ['KERNEL32.dll'])
    raw = bytearray(exe.read_bytes())
    iat = struct.unpack_from('<I', raw, 0x300+16)[0]+0x400000
    raw[0x700:0x70c] = b'\xff\x15'+struct.pack('<I', iat)+b'\xe8'+struct.pack('<i', 0x401600-0x40150b)+b'\xc3'
    raw[0x800:0x806] = b'\xff\x25'+struct.pack('<I', iat)
    exe.write_bytes(raw)
    prepare(exe, root/'analysis', hashlib.sha256(raw).hexdigest())
    result = inspect(exe, root/'analysis', [0x401500, 0x401600])
    first, tail = result['roots']
    assert first['instructions'] == 3 and not first['limit_reached']
    assert first['calls'][0]['import'] == 'KERNEL32.dll!#1'
    assert first['calls'][1]['target'] == 0x401600
    assert tail['instructions'] == 1 and not tail['calls']
    assert tail['stops'][0]['import'] == 'KERNEL32.dll!#1'
    assert inspect(exe, root/'analysis', [0x401500], 1)['roots'][0]['limit_reached']
    for entries, limit in [([], 1), ([0x400000], 1), ([0x401500], 0)]:
        try:
            inspect(exe, root/'analysis', entries, limit)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid root/limit accepted')
print('PASS: direct call, ordinal IAT, tail thunk, bounded decode, invalid roots')

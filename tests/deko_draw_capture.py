"""Synthetic protocol tests, not game data or a GPU rendering test."""
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_deko_draw_capture import read_capture, sources, repack


def fixture():
    header = struct.pack('<II', 0x44564E46, 1)
    regs = bytearray(0x5003 * 4)
    regs[0x4000 * 4:0x4000 * 4 + 16] = b'A' * 16
    regs[(0x4000 + 255 * 4) * 4:(0x4000 + 256 * 4) * 4] = b'Z' * 16
    regs[0x4400 * 4:0x4400 * 4 + 16] = b'P' * 16
    records = {(1, 0): regs, (2, 0): bytes(512), (3, 0): bytes(24),
               (4, 0): bytes(16), (5, 0): struct.pack('<4Q', 1, 0, 0, 1 << 63),
               (5, 1): struct.pack('<4Q', 1, 0, 0, 0), (6, 0x2000): bytes(128),
               (7, 0x1000): bytes(192)}
    binary = header + b''.join(struct.pack('<III', kind, address, len(data)) + data
                               for (kind, address), data in records.items())
    return binary, {'format': 1, 'index_format': 0, 'count': 96, 'index_base': 0x1000}


class CaptureTests(unittest.TestCase):
    def test_sparse_constant_order_and_stage(self):
        data, manifest = fixture()
        result = read_capture(data, manifest)
        self.assertEqual(sources(result, 0)[1], b'A' * 16 + b'Z' * 16)
        self.assertEqual(sources(result, 1)[2], b'P' * 16)

    def test_incomplete_capture_rejected(self):
        data, manifest = fixture()
        for cut in (1, 4, 11, 100, len(data) - 8):
            with self.assertRaises(ValueError):
                read_capture(data[:-cut], manifest)

    def test_wrong_index_count_rejected(self):
        data, manifest = fixture()
        manifest['count'] = 97
        with self.assertRaises(ValueError):
            read_capture(data, manifest)

    def test_physical_overflow_rejected(self):
        data, manifest = fixture()
        data += struct.pack('<III', 6, 0x1FFFFFFF, 2) + b'xx'
        with self.assertRaises(ValueError):
            read_capture(data, manifest)

    def test_copy_preserves_bits_and_padding(self):
        block = {'source_bytes': 8, 'native_bytes': 32, 'fields': [
            {'source_offset': 0, 'native_offset': 0, 'elements': 2,
             'element_bytes': 4, 'source_stride': 4, 'native_stride': 16}]}
        bits = struct.pack('<II', 0x80000000, 0x7FC00123)
        self.assertEqual(repack(bits, block), bits[:4] + bytes(12) + bits[4:] + bytes(12))
        block['fields'][0]['native_stride'] = 2
        with self.assertRaises(ValueError):
            repack(bits, block)


if __name__ == '__main__':
    unittest.main()

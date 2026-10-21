"""Synthetic PE/symbol fixtures for host-PC attribution, without game inputs."""
import struct
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from symbolize_host_fault import locate, pe_layout


def fixture(wide=True):
    raw = bytearray(1024)
    raw[:2] = b'MZ'
    struct.pack_into('<I', raw, 60, 0x80)
    raw[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<H', raw, 0x86, 2)
    optional = 0x98
    struct.pack_into('<H', raw, 0x94, 0xf0)
    struct.pack_into('<H', raw, optional, 0x20b if wide else 0x10b)
    struct.pack_into('<Q' if wide else '<I', raw, optional+(24 if wide else 28),
                     0x140000000 if wide else 0x400000)
    struct.pack_into('<I', raw, optional+56, 0x6000)
    section = optional+0xf0
    struct.pack_into('<II', raw, section+8, 0x2000, 0x1000)
    struct.pack_into('<I', raw, section+36, 0x60000020)
    struct.pack_into('<II', raw, section+40+8, 0x1000, 0x4000)
    struct.pack_into('<I', raw, section+40+36, 0xc0000040)
    return bytes(raw)


class HostFaultTests(unittest.TestCase):
    def test_rebase_pc_and_keep_historical_call_separate(self):
        log = ('FNV_HOST_EXCEPTION code=0xc0000005 pc=0x7ff700001235 '
               'image_offset=0x1235 guest_site=0x401234 thread=17\n')
        symbols = ('140001100 T caller\n140001200 T actual_function\n'
                   '140001230 D unrelated_data\n140001240 T next_function\n')
        result = locate(fixture(), log, symbols)['faults'][0]
        self.assertEqual(result['runtime_image_base'], 0x7ff700000000)
        self.assertEqual(result['linked_pc'], 0x140001235)
        self.assertEqual(result['last_indirect_site'], 0x401234)
        self.assertEqual(result['preceding_text_symbol'],
                         dict(address=0x140001200, names=['actual_function'], offset=0x35))

    def test_new_log_label_and_aliases(self):
        log = ('FNV_HOST_EXCEPTION code=0xc0000005 pc=0x7ff700001200 '
               'image_offset=0x1200 last_indirect_site=0x402000 thread=99\n')
        result = locate(fixture(), log, '140001200 t local_alias\n140001200 T fn\n')['faults'][0]
        self.assertEqual(result['preceding_text_symbol']['names'], ['fn', 'local_alias'])
        self.assertEqual(result['preceding_text_symbol']['offset'], 0)

    def test_missing_symbol_is_explicit(self):
        log = ('FNV_HOST_EXCEPTION code=0xc0000005 pc=0x10001100 '
               'image_offset=0x1100 guest_site=0x400000 thread=1\n')
        self.assertIsNone(locate(fixture(), log, '140001200 T later\n')['faults'][0]['preceding_text_symbol'])

    def test_data_and_out_of_image_offsets_rejected(self):
        for offset in (0xfff, 0x3000, 0x4000, 0x6000):
            log = (f'FNV_HOST_EXCEPTION code=0x1 pc=0x{0x10000000+offset:x} '
                   f'image_offset=0x{offset:x} guest_site=0x0 thread=1')
            with self.assertRaisesRegex(ValueError, 'outside executable'):
                locate(fixture(), log, '')

    def test_missing_record_and_truncated_pe(self):
        with self.assertRaisesRegex(ValueError, 'No host exception'):
            locate(fixture(), 'no crash', '')
        for size in (0, 63, 127, 155, 400):
            with self.assertRaises(ValueError):
                pe_layout(fixture()[:size])

    def test_pe32_base(self):
        log = ('FNV_HOST_EXCEPTION code=0x1 pc=0x501250 '
               'image_offset=0x1250 guest_site=0x0 thread=1')
        result = locate(fixture(False), log, '00401200 T x86_function')['faults'][0]
        self.assertEqual(result['linked_pc'], 0x401250)
        self.assertEqual(result['preceding_text_symbol']['offset'], 0x50)


if __name__ == '__main__':
    unittest.main()

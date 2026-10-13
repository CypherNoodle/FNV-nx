import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from inspect_deko_texture_capture import validate


class TextureTests(unittest.TestCase):
    def fixture(self):
        texture = {'slot': 2, 'fetch': [2, 64, 0, 0, 0, 512], 'endian': 1,
                   'dimension': 1, 'max_level': 0, 'width': 4, 'height': 4, 'depth': 1,
                   'format': 18, 'base_bytes': 8, 'base_address': 4096, 'base_status': 1,
                   'mip_bytes': 0, 'mip_address': 0, 'mip_status': 0}
        return {'format': 1, 'cpu_snapshot': True, 'texture_mask': 4, 'skipped': 0,
                'textures': [texture]}

    def test_valid_region(self):
        report = validate(self.fixture(), lambda name: b'12345678')
        self.assertEqual(report['captured_bytes'], 8)
        self.assertFalse(report['gpu_coherence_verified'])

    def test_truncated_region(self):
        with self.assertRaises(ValueError):
            validate(self.fixture(), lambda name: b'123')

    def test_unavailable_snapshot_does_not_read_stale_file(self):
        metadata = self.fixture(); metadata['textures'][0]['base_status'] = -1
        def fail(name): raise AssertionError('Must not read a failed snapshot')
        self.assertEqual(validate(metadata, fail)['captured_bytes'], 0)

    def test_duplicate_slot_rejected(self):
        metadata = self.fixture(); metadata['textures'].append(copy.deepcopy(metadata['textures'][0]))
        with self.assertRaises(ValueError): validate(metadata, lambda name: b'12345678')

    def test_physical_overflow_and_descriptor_mismatch(self):
        metadata = self.fixture(); metadata['textures'][0]['base_address'] = 0x1FFFFFFF
        with self.assertRaises(ValueError): validate(metadata, lambda name: b'12345678')
        metadata = self.fixture(); metadata['textures'][0]['endian'] = 0
        with self.assertRaises(ValueError): validate(metadata, lambda name: b'12345678')


if __name__ == '__main__': unittest.main()

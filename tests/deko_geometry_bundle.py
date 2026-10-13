import struct
import unittest
from deko_draw_capture import fixture, read_capture
from build_deko_geometry_bundle import build


class BundleTests(unittest.TestCase):
    def prepared(self):
        binary, draw = fixture()
        records = read_capture(binary, draw)
        registers = bytearray(records[1, 0])
        struct.pack_into('<II', registers, (0x4800 + 95 * 2) * 4, 0x2003, 32 << 2)
        records[1, 0] = bytes(registers)
        system = bytearray(records[2, 0])
        struct.pack_into('<I', system, 4, draw['index_base'])
        records[2, 0] = bytes(system)
        draw.update(vertex_hash='1234', vertex_modification='0001', primitive=6, primitive_restart=False)
        def block(binding, size):
            return {'set': 1, 'binding': binding, 'source_bytes': size, 'native_bytes': size,
                    'fields': [{'source_offset': 0, 'native_offset': 0, 'elements': 1,
                                'element_bytes': size, 'source_stride': size, 'native_stride': size}]}
        abi = {'shader': 'shader_1234_0001.vert', 'native_sampler_bindings': [],
               'resources': {'ssbos': [{'name': 'XeSharedMemory'}]},
               'constant_blocks': [block(0, 512), block(1, 32), block(3, 160), block(4, 768)]}
        return records, draw, abi

    def test_original_memory_bytes_and_relocated_fetch(self):
        records, draw, abi = self.prepared()
        binary, changes = build(records, draw, abi)
        header = struct.unpack_from('<20I', binary)
        shared = header[19]
        self.assertEqual(binary[shared:shared + 128], records[6, 0x2000])
        self.assertEqual(binary[header[8]:header[8] + header[9]], records[7, 0x1000])
        self.assertEqual(changes, [{'slot': 95, 'original': 0x2000, 'native': 0}])
        self.assertEqual(struct.unpack_from('<I', binary, header[16] + 95 * 8)[0], 3)
        self.assertEqual(struct.unpack_from('<I', binary, header[10] + 4)[0], 256)
        self.assertEqual(binary[shared + 256:shared + 256 + 192], records[7, 0x1000])

    def test_uncaptured_address_rejected(self):
        records, draw, abi = self.prepared()
        system = bytearray(records[2, 0]); struct.pack_into('<I', system, 4, 0x3000)
        records[2, 0] = bytes(system)
        with self.assertRaises(ValueError):
            build(records, draw, abi)

    def test_vertex_texture_and_restart_rejected(self):
        records, draw, abi = self.prepared()
        draw['primitive_restart'] = True
        with self.assertRaises(ValueError):
            build(records, draw, abi)
        draw['primitive_restart'] = False
        abi['native_sampler_bindings'] = [{'native_binding': 0}]
        with self.assertRaises(ValueError):
            build(records, draw, abi)


if __name__ == '__main__':
    unittest.main()

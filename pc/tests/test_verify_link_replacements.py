"""Reject compiling a replacement but accidentally linking its old object."""
import sys
import tempfile
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from verify_link_replacements import verify


class ReplacementTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.old = self.root/'old/unit.o'
        self.new = self.root/'new/unit.o'
        for path in (self.old, self.new):
            path.parent.mkdir()
            path.write_bytes(path.parent.name.encode())
        self.response = self.root/'link.rsp'

    def write(self, *objects):
        self.response.write_text('\n'.join('"'+str(p)+'"' for p in
                                 (*objects, '-Wl,--stack,16777216', '-o', self.root/'probe.exe'))+'\n')

    def test_exact_object_and_hash(self):
        self.write(self.new)
        result = verify(self.response, [self.new])
        self.assertEqual(result['status'], 'LINK_REPLACEMENTS_VERIFIED')
        self.assertEqual(result['replacements'][0]['object'], str(self.new.resolve()))
        self.assertEqual(len(result['replacements'][0]['sha256']), 64)

    def test_old_object_rejected_even_when_new_object_exists(self):
        self.write(self.old)
        with self.assertRaisesRegex(ValueError, 'stale'):
            verify(self.response, [self.new])

    def test_old_and_new_together_rejected(self):
        self.write(self.old, self.new)
        with self.assertRaisesRegex(ValueError, 'ambiguous'):
            verify(self.response, [self.new])

    def test_duplicate_object_or_request_rejected(self):
        self.write(self.new, self.new)
        with self.assertRaisesRegex(ValueError, 'Duplicate'):
            verify(self.response, [self.new])
        self.write(self.new)
        with self.assertRaisesRegex(ValueError, 'unique'):
            verify(self.response, [self.new, self.new])

    def test_missing_object_and_malformed_response(self):
        self.write(self.new)
        self.new.unlink()
        with self.assertRaisesRegex(ValueError, 'missing'):
            verify(self.response, [self.new])
        self.response.write_text('-o probe.exe')
        with self.assertRaisesRegex(ValueError, 'quoted'):
            verify(self.response, [self.old])


if __name__ == '__main__':
    unittest.main()

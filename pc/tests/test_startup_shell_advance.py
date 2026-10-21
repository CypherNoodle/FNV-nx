"""Reject stale regressions and unsafe reuse of changed platform dependencies."""
import sys
import json
import tempfile
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from advance_startup_shell import dependency_guard, permitted_changes, require_current_proof, sha
from replay_startup_math import diagnostic_file_root, decode_cache, encode, verified_seed


class RefreshGuards(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.pc = Path(self.temp.name)
        (self.pc / 'native').mkdir()
        self.abi = self.pc / 'native/windows_thread.h'
        self.abi.write_text('old')
        self.game = self.pc / 'game.c'
        self.game.write_text('void game(void) {}')
        self.forced = self.pc / 'runtime.h'
        self.forced.write_text('')

    def tearDown(self):
        self.temp.cleanup()

    def test_changed_game_or_cpu_interface_rejected(self):
        frozen = {str(self.game): sha(self.game), str(self.abi): sha(self.abi)}
        self.abi.write_text('new')
        self.assertEqual(permitted_changes(frozen, self.pc), {self.abi})
        self.game.write_text('modified guest code')
        with self.assertRaises(ValueError):
            permitted_changes(frozen, self.pc)

    def test_user32_provider_is_audited_platform_change(self):
        header=self.pc / 'native/windows_user32.h'
        source=self.pc / 'native/windows_user32.c'
        header.write_text('old header');source.write_text('old source')
        frozen={str(header):sha(header),str(source):sha(source)}
        header.write_text('new header');source.write_text('new source')
        self.assertEqual(permitted_changes(frozen,self.pc),{header,source})
        self.game.write_text('#include "windows_user32.h"\n')
        with self.assertRaises(ValueError):
            dependency_guard(self.game,self.forced,{header},{header})
        proof=dict(status='PASS',source_sha256={str(source):sha(source)})
        with self.assertRaises(ValueError):
            require_current_proof(proof,[source,header])

    def test_direct_header_dependency_rejected(self):
        self.game.write_text('#include "windows_thread.h"\n')
        with self.assertRaises(ValueError):
            dependency_guard(self.game, self.forced, {self.abi}, {self.abi})

    def test_transitive_forced_header_dependency_rejected(self):
        middle = self.pc / 'middle.h'
        middle.write_text('#include "windows_thread.h"\n')
        self.forced.write_text('#include "middle.h"\n')
        with self.assertRaises(ValueError):
            dependency_guard(self.game, self.forced, {self.abi, middle}, {self.abi})

    def test_missing_include_rejected(self):
        self.game.write_text('#include "unknown.h"\n')
        with self.assertRaises(ValueError):
            dependency_guard(self.game, self.forced, {self.abi}, {self.abi})

    def test_unrelated_game_unit_reusable(self):
        dependency_guard(self.game, self.forced, {self.abi}, {self.abi})

    def test_current_regression_required(self):
        proof = dict(status='PASS', source_sha256={str(self.abi): sha(self.abi)})
        require_current_proof(proof, [self.abi])
        self.abi.write_text('changed')
        with self.assertRaises(ValueError):
            require_current_proof(proof, [self.abi])
        proof['status'] = 'FAIL'
        with self.assertRaises(ValueError):
            require_current_proof(proof, [])

    def test_replay_keeps_selected_storage_root(self):
        storage=self.pc/'storage'
        storage.mkdir()
        exe=self.pc/'game.exe'
        self.assertEqual(diagnostic_file_root({},exe),self.pc)
        self.assertEqual(diagnostic_file_root({'diagnostic_file_root':str(storage)},exe),storage)
        for root in ('relative',str(self.pc/'missing'),str(self.game)):
            with self.assertRaises(ValueError):
                diagnostic_file_root({'diagnostic_file_root':root},exe)

    def test_seed_cache_rejects_truncation_duplicates_and_mutation(self):
        record=(bytes(88),0,bytes(86))
        data=encode([record])
        self.assertEqual(decode_cache(data),[record])
        for bad in (data[:-1],data+b'X',b'BADMAGIC'+data[8:],data[:12]+b'\x01'+data[13:]):
            with self.assertRaises(ValueError):
                decode_cache(bad)
        with self.assertRaises(ValueError):
            encode([record,record])

    def test_seed_rechecks_exact_target_and_provenance(self):
        record=(bytes(88),0,bytes(86));cache=self.pc/'cache-0001.bin'
        cache.write_bytes(encode([record]));sources={str(self.abi):sha(self.abi)}
        proof=dict(status='EXACT_TARGET_REPLAY_STOPPED',records=1,numerical_model='test',
                   source_sha256={**sources,str(cache):sha(cache)},
                   observations=[dict(cache_sha256=sha(cache))])
        path=self.pc/'proof.json';path.write_text(json.dumps(proof))
        class Target:
            report={'model':'test'}
            def evaluate(self,request):return (0,bytes(86))
        self.assertEqual(verified_seed(path,Target(),sources,{}),[record])
        class Wrong(Target):
            def evaluate(self,request):return (0,b'X'+bytes(85))
        with self.assertRaises(ValueError):verified_seed(path,Wrong(),sources,{})
        with self.assertRaises(ValueError):verified_seed(path,Target(),{str(self.abi):'stale'}, {})
        cache.write_bytes(cache.read_bytes()+b'X')
        with self.assertRaises(ValueError):verified_seed(path,Target(),sources,{})


if __name__ == '__main__':
    unittest.main()

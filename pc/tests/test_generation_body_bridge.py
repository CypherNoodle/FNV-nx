"""Own C fixtures for private-generation comparison guards; no game data."""
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_generation_body_bridge import audit,bodies,TRACE_DEFINE


class BodyBridgeTests(unittest.TestCase):
    def fixture(self,base,candidate,header=None,supplement=None):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as d:
            root=Path(d);left=root/'base';right=root/'candidate';left.mkdir();right.mkdir()
            (left/'recomp_0000.c').write_text(base);(right/'generated_0000.c').write_text(candidate)
            extras=[]
            if supplement is not None:
                p=root/'extra.c';p.write_text(supplement);extras.append(p)
            h=None
            if header is not None:h=root/'trace.h';h.write_text(header)
            return audit(left,right,extras,h)

    def test_braces_and_fake_functions_in_comments(self):
        text='''/*
void fake(void) { }
*/
void sub_00001000(void) {
    const char *s="braces { }"; /* } */
    if(s){ (void)s; }
}
// void other(void) { }
void xbe_entry_point(void) { }
'''
        self.assertEqual([name for name,_ in bodies(text)],['sub_00001000','xbe_entry_point'])
        self.assertEqual(self.fixture(text,text)['identical_functions'],2)

    def test_changed_body_is_not_accepted(self):
        r=self.fixture('void sub_00001000(void) { return; }','void sub_00001000(void) { abort(); }')
        self.assertEqual(r['status'],'BODY_MISMATCH')
        self.assertEqual(r['different_functions'],['sub_00001000'])

    def test_additional_function_is_reported_not_registered(self):
        s='void sub_00001000(void) { }'
        r=self.fixture(s,s,supplement='void sub_00002000(void) { }')
        self.assertEqual(r['status'],'BODY_MATCH');self.assertEqual(list(r['candidate_only']),['sub_00002000'])
        self.assertFalse(r['functions_registered']);self.assertFalse(r['objects_compiled'])

    def test_duplicate_or_empty_input_rejected(self):
        s='void sub_00001000(void) { }'
        with self.assertRaisesRegex(ValueError,'Duplicate'):self.fixture(s,s,supplement=s)
        with self.assertRaisesRegex(ValueError,'No generated functions'):self.fixture('/* empty */','/* empty */')

    def test_unterminated_function_rejected(self):
        with self.assertRaisesRegex(ValueError,'Unterminated'):list(bodies('void sub_00001000(void) { if(1) { }'))

    def test_trace_wrapper_requires_exact_definition(self):
        a='void sub_00001000(void) { RECOMP_ICALL_SAFE_AT(1,2,3); }'
        b=a.replace('RECOMP_ICALL_SAFE_AT','FNV_STARTUP_ICALL')
        self.assertEqual(self.fixture(a,b)['status'],'BODY_MISMATCH')
        self.assertEqual(self.fixture(a,b,header=TRACE_DEFINE+'\n')['status'],'BODY_MATCH')
        with self.assertRaisesRegex(ValueError,'Unverified'):self.fixture(a,b,header=TRACE_DEFINE.replace('fnv_probe_site=(site);','abort();'))


if __name__=='__main__':unittest.main()

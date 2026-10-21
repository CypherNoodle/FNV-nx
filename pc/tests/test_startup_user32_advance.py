"""Reject unintended changes when adding USER32 to a frozen diagnostic."""
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from advance_startup_user32 import permitted_changes,sha

def test():
    with TemporaryDirectory(dir=Path(__file__).resolve().parents[2]/'local') as tmp:
        pc=Path(tmp)
        files={'tests/captured_startup_host.c':'host','tools/probe_captured_startup.py':'builder','native/abi.h':'abi'}
        for n,s in files.items():
            p=pc/n;p.parent.mkdir(exist_ok=True);p.write_text(s)
        frozen={str(pc/n):sha(pc/n) for n in files}
        for n in ('native/windows_user32.c','tests/captured_startup_host.c','tests/windows_user32_host.c'):
            (pc/n).write_text('#include "windows_user32.h"\n')
        (pc/'tools/probe_captured_startup.py').write_text('new builder')
        expected={(pc/n).resolve() for n in ('tests/captured_startup_host.c','tools/probe_captured_startup.py')}
        assert permitted_changes(frozen,pc)==expected;checks=1
        def denied():
            nonlocal checks
            try:permitted_changes(frozen,pc)
            except ValueError:checks+=1
            else:raise AssertionError('Unsafe USER32 adapter reuse')
        (pc/'native/abi.h').write_text('changed ABI');denied();(pc/'native/abi.h').write_text('abi')
        (pc/'native/other.c').write_text('#include "windows_user32.h"\n');denied();(pc/'native/other.c').unlink()
        (pc/'tools/probe_captured_startup.py').write_text('builder');denied()
    print('PASS',checks,'USER32 dependency guards')
if __name__=='__main__':test()

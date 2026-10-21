"""Reject untracked ABI/dependency changes in INI startup refreshes."""
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from advance_startup_profiles import permitted_changes,sha

def test():
    with TemporaryDirectory(dir=Path(__file__).resolve().parents[2]/'local') as tmp:
        pc=Path(tmp)
        files={'native/windows_thread.c':'thread','tests/captured_startup_host.c':'host','native/abi.h':'abi'}
        for n,s in files.items():
            p=pc/n;p.parent.mkdir(exist_ok=True);p.write_text(s)
        frozen={str(pc/n):sha(pc/n) for n in files}
        for n in ('native/windows_thread.c','tests/captured_startup_host.c','tests/windows_profiles_host.c','switch/crt_probe.c'):
            p=pc/n;p.parent.mkdir(exist_ok=True);p.write_text('#include "windows_profiles.h"\n')
        native=pc/'native/windows_thread.c';native.write_text(native.read_text()+'#include "windows_profiles.inc"\n')
        expected={(pc/n).resolve() for n in ('native/windows_thread.c','tests/captured_startup_host.c')}
        assert permitted_changes(frozen,pc)==expected;checks=1
        def denied():
            nonlocal checks
            try:permitted_changes(frozen,pc)
            except ValueError:checks+=1
            else:raise AssertionError('Unsafe profile refresh')
        (pc/'native/abi.h').write_text('changed ABI');denied();(pc/'native/abi.h').write_text('abi')
        other=pc/'native/other.c';other.write_text('#include "windows_profiles.inc"\n');denied();other.unlink()
        other.write_text('#include "windows_profiles.h"\n');denied();other.unlink()
        (pc/'tests/captured_startup_host.c').write_text('host');denied()
    print('PASS',checks,'profile dependency guards')
if __name__=='__main__':test()

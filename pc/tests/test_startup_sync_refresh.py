"""Fail-closed dependency checks for the single native-unit refresh."""
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from refresh_startup_sync import permitted_changes,sha

def test():
    checks=0
    scratch=Path(__file__).resolve().parents[2]/"local"
    with TemporaryDirectory(dir=scratch) as tmp:
        root=Path(tmp);files={'windows_thread.c':'#include "windows_events.inc"\n#include "windows_workers.inc"\n',
                            'windows_events.inc':'events','windows_workers.inc':'workers','abi.h':'shared'}
        for n,s in files.items():(root/n).write_text(s)
        frozen={str(p):sha(p) for p in root.iterdir()}
        def denied():
            nonlocal checks
            try:permitted_changes(frozen,root)
            except ValueError:checks+=1
            else:raise AssertionError('Accepted unsafe refresh')
        denied()
        for name in ('windows_thread.c','windows_events.inc','windows_workers.inc'):
            p=root/name;p.write_text(files[name]+' changed')
            assert permitted_changes(frozen,root)=={p.resolve()};checks+=1
            p.write_text(files[name])
        (root/'windows_events.inc').write_text('changed events')
        (root/'abi.h').write_text('changed ABI');denied();(root/'abi.h').write_text('shared')
        other=root/'other.c';other.write_text('#include "windows_events.inc"\n');denied();other.unlink()
        (root/'windows_thread.c').write_text('missing owner');denied()
    print('PASS',checks,'dependency guards')
if __name__=='__main__':test()

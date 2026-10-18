"""Byte normalization and path safety, without a Git checkout."""
from pathlib import Path
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from check_line_endings import check

with tempfile.TemporaryDirectory() as temporary:
    root=Path(temporary);(root/'pc').mkdir();file=root/'pc/sample.c'
    file.write_bytes(b'one\r\ntwo\n')
    assert check(root,['pc/sample.c'])['crlf_files']==['pc/sample.c'] or check(root,['pc/sample.c'])['crlf_files']==['pc\\sample.c']
    assert file.read_bytes()==b'one\r\ntwo\n'
    assert check(root,['pc/sample.c'],True)['fixed'] and file.read_bytes()==b'one\ntwo\n'
    assert not check(root,['pc/sample.c'])['crlf_files']
    for name in ['../elsewhere.c',str(file.resolve())]:
        try:check(root,[name],True)
        except ValueError:pass
        else:raise AssertionError('Unsafe path accepted')
    file.write_bytes(b'abc\0def')
    try:check(root,['pc/sample.c'],True)
    except ValueError:pass
    else:raise AssertionError('Binary accepted')
print('PASS: LF check, explicit fix, idempotence, path escape and binary rejection')

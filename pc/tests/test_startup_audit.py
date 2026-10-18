"""Synthetic startup identities; a parsed entry must never imply game boot."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from audit_startup import audit
from prepare_aot_image import prepare
from pe_fixture import write_pe


def rejected(action):
    try:
        action()
    except ValueError:
        return
    raise AssertionError('Invalid identity accepted')


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    exe = root/'fixture.exe'
    write_pe(exe)
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    assert audit(exe)['status'] == 'BLOCKED'
    prepare(exe, root/'plain', digest)
    result = audit(exe, root/'plain')
    assert result['changed_text_bytes'] == 0 and not result['engine_started']
    capture = root/'text.bin'
    capture.write_bytes(b'\x90'*(4096-512))
    prepare(exe, root/'captured', digest, capture,
            hashlib.sha256(capture.read_bytes()).hexdigest(), 0x401004)
    result = audit(exe, root/'captured')
    assert result['changed_text_bytes'] > 0 and len(result['reasons']) == 3
    p = root/'captured/input-profile.json'
    saved = p.read_text()
    modified = json.loads(saved)
    modified['code_source'] = 'file-text'
    p.write_text(json.dumps(modified))
    rejected(lambda: audit(exe, root/'captured'))
    p.write_text(saved)
    container = root/'captured/fnv-analysis.xbe'
    container.write_bytes(container.read_bytes()+b'corrupt')
    rejected(lambda: audit(exe, root/'captured'))
    write_pe(exe, section='.bind')
    result = audit(exe)
    assert result['entry_section'] == '.bind' and len(result['reasons']) == 2
    rejected(lambda: audit(exe, root/'plain'))
    # Setting flags in a profile is not execution evidence.
    write_pe(exe)
    p = root/'plain/input-profile.json'
    modified = json.loads(p.read_text())
    modified.update(cold_boot_supported=True, original_startup_entry_verified=True)
    p.write_text(json.dumps(modified))
    assert audit(exe, root/'plain')['status'] == 'BLOCKED'
print('PASS: plain/captured/packed entries, identity failures, no inferred engine boot')

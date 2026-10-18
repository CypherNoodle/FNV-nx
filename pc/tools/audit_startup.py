"""Read-only startup identity audit. Never executes or rewrites game code."""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from audit_install import pe_info


def sha(data):
    return hashlib.sha256(data).hexdigest()


def audit(exe, analysis=None):
    raw = exe.read_bytes()
    pe = pe_info(exe)
    if sha(raw) != pe['sha256']:
        raise ValueError('Executable changed during audit')
    reasons = ['No verified cold-start execution profile']
    report = dict(status='BLOCKED', executable_sha256=pe['sha256'],
                  file_entry=pe['entry_va'], file_entry_hex=hex(pe['entry_va']),
                  entry_section=pe['entry_section'],
                  normal_import_slots=sum(len(i['symbols']) for i in pe['imports']),
                  engine_started=False, switch_execution_verified=False,
                  reasons=reasons, drm_free=None)
    if pe['entry_section'] == '.bind':
        reasons.append('File entry is in .bind; it is not the translated .text entry')
    if analysis is not None:
        profile_bytes = (analysis/'input-profile.json').read_bytes()
        p = json.loads(profile_bytes)
        container = (analysis/'fnv-analysis.xbe').read_bytes()
        if (p['executable_sha256'] != pe['sha256'] or
                p['original_file_entry'] != pe['entry_va'] or
                p['image_base'] != pe['image_base'] or
                p['image_bytes'] != pe['image_bytes'] or
                p['analysis_container_sha256'] != sha(container)):
            raise ValueError('EXE/profile/container identity mismatch')
        if len(container) < 4096 or container[:4] != b'XBEH':
            raise ValueError('Invalid private analysis container')
        count, table = struct.unpack_from('<II', container, 0x11c)
        table -= pe['image_base']
        if not 0 < count <= 32 or not 0 <= table <= 4096-count*56:
            raise ValueError('Invalid analysis section table')
        text = [s for s in pe['sections'] if s['name'] == '.text']
        if len(text) != 1:
            raise ValueError('Require exactly one .text section')
        s = text[0]
        rows = [struct.unpack_from('<5I', container, table+i*56) for i in range(count)]
        matches = [r for r in rows if r[1] == pe['image_base']+s['rva']]
        if len(matches) != 1:
            raise ValueError('Analysis .text mapping mismatch')
        _, _, size, offset, length = matches[0]
        if offset < 4096 or offset+length > len(container) or length > size:
            raise ValueError('Invalid analysis .text range')
        code = container[offset:offset+length]
        if sha(code) != p['text_sha256'] or len(code) < s['virtual_bytes']:
            raise ValueError('Analysis text identity/length mismatch')
        if not pe['image_base']+s['rva'] <= p['analysis_entry'] < pe['image_base']+s['rva']+s['virtual_bytes']:
            raise ValueError('Analysis entry outside .text')
        disk = raw[s['raw_offset']:s['raw_offset']+s['raw_bytes']]
        disk = (disk+b'\0'*max(0, s['virtual_bytes']-len(disk)))[:s['virtual_bytes']]
        changed = sum(a != b for a, b in zip(disk, code))
        report.update(profile_sha256=sha(profile_bytes), analysis_container_sha256=sha(container),
                      code_source=p['code_source'], analysis_entry=p['analysis_entry'],
                      analysis_entry_hex=hex(p['analysis_entry']),
                      compared_text_bytes=len(disk), changed_text_bytes=changed,
                      file_text_sha256=sha(disk), analysis_text_sha256=sha(code))
        if p['code_source'] != 'file-text':
            reasons.append('Analysis uses captured code, not the on-disk executable text')
        if p['analysis_entry'] != pe['entry_va']:
            reasons.append('Analysis entry differs from the file startup entry')
        if p['code_source'] == 'file-text' and changed:
            raise ValueError('file-text profile contains changed code')
        if (analysis/'input-profile.json').read_bytes() != profile_bytes or (analysis/'fnv-analysis.xbe').read_bytes() != container:
            raise ValueError('Analysis changed during audit')
    if exe.read_bytes() != raw:
        raise ValueError('Executable changed during audit')
    report['limits'] = ['Identity audit only; profile flags cannot prove startup',
                        'Imports do not prove provider behavior or absence of DRM',
                        'No raw code, translation, patching, or game execution']
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--analysis', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    a = parser.parse_args()
    result = audit(a.exe, a.analysis)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('x') as f:
        json.dump(result, f, indent=2)
        f.write('\n')
    print(result['status'], result['entry_section'], result['file_entry_hex'])

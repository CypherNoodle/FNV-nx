"""Audit evidence against interval oracle and synthetic x86; no game execution."""
import argparse
import json
from pathlib import Path
import random
import struct
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from audit_aot_gaps import Intervals, audit, classify, sections_from_container, sha
from prepare_aot_image import prepare
from pe_fixture import write_pe


def test(temp_dir, deps):
    rng = random.Random(1719)
    pairs = sorted({(a, a+rng.randrange(1, 80)) for a in
                    (rng.randrange(0x1000, 0x2000) for _ in range(300))})
    pairs += [(0x1000, 0x3000), (0x1001, 0x1002)]
    index = Intervals([dict(start=a, end=b) for a, b in pairs])
    for value in [0x1000, 0x1001, 0x1002, 0x3000]+[rng.randrange(0xff0, 0x3010) for _ in range(2000)]:
        assert index.owners(value) == sorted({(a, b) for a, b in pairs if a <= value < b})
    for pair in [(8, 8), (9, 8), (-1, 8), (8, 0x100000001)]:
        try:
            Intervals([dict(start=pair[0], end=pair[1])])
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid interval accepted')
    with tempfile.TemporaryDirectory(prefix='fnv-aot-gaps-', dir=temp_dir) as tmp:
        root = Path(tmp)
        exe = root/'fixture.exe'
        write_pe(exe, ('KERNEL32.dll',))
        raw = bytearray(exe.read_bytes())
        # mov eax, 0x12345678; nop; ret. +2 is inside a five-byte instruction.
        raw[0x200:0x207] = bytes.fromhex('b87856341290c3')
        struct.pack_into('<I', raw, 0x98+224+36, 0x60000020)
        exe.write_bytes(raw)
        analysis = root/'analysis'
        profile = prepare(exe, analysis, sha(raw))
        base = profile['image_base']+0x1000
        functions = [dict(start=hex(base), end=hex(base+7)),
                     dict(start=hex(base+5), end=hex(base+6))]
        targets = [base, base+2, base+5, base+12, profile['image_base']+0x5000]
        refs = [dict(to=hex(target), **{'from': hex(base+5)}, type='call') for target in targets[:-1]]
        scan_dir = analysis/'disasm'
        scan_dir.mkdir()
        for name, value in [('functions.json', functions), ('xrefs.json', refs)]:
            (scan_dir/name).write_text(json.dumps(value))
        scan = dict(input_sha256=profile['analysis_container_sha256'],
                    sdk_sha256='synthetic-sdk', index_wrapper_sha256='synthetic-index',
                    files={p.name: sha(p.read_bytes()) for p in scan_dir.iterdir()})
        (analysis/'scan-identity.json').write_text(json.dumps(scan))
        generated = analysis/'generated'
        generated.mkdir()
        stubs = ''.join('void sub_%08X(void) { recomp_unimpl("untranslated call target", 0x%08Xu); }\n' %
                        (target, target) for target in targets).encode()
        stub = generated/'recomp_stubs_unresolved.c'
        stub.write_bytes(stubs)
        report = dict(input_profile_sha256=sha((analysis/'input-profile.json').read_bytes()),
                      sdk_sha256=scan['sdk_sha256'], index_wrapper_sha256=scan['index_wrapper_sha256'],
                      output_files={stub.name: sha(stubs)}, gaps=dict(unresolved_call_targets=len(targets)))
        (analysis/'generation-report.json').write_text(json.dumps(report))
        before = {p: sha(p.read_bytes()) for p in analysis.rglob('*') if p.is_file()}
        output = root/'audit.json'
        result = audit(analysis, output, deps)
        container = (analysis/'fnv-analysis.xbe').read_bytes()
        legacy_profile = json.loads(json.dumps(profile))
        for section in legacy_profile['sections']:
            del section['analysis_raw_bytes']
        assert sections_from_container(container, legacy_profile) == sections_from_container(container, profile)
        # Captured-text legacy profiles also record virtual length rather than
        # PE raw padding; verify this compatibility with a distinct-sized input.
        capture = root/'capture.bin'
        struct.pack_into('<I', raw, 0x98+224+8, 0xD00)
        exe.write_bytes(raw)
        capture.write_bytes(bytes(raw[0x200:0xF00]))
        captured_dir = root/'captured'
        captured = prepare(exe, captured_dir, sha(raw), capture, sha(capture.read_bytes()), base)
        del captured['sections'][0]['analysis_raw_bytes']
        assert sections_from_container((captured_dir/'fnv-analysis.xbe').read_bytes(), captured)[0]['stored'] == 0xD00
        assert result['unresolved_targets'] == 5
        assert result['classification_counts'] == {'detected-entry': 2, 'inside-detected-body': 1,
            'executable-gap': 1, 'outside-image': 1}
        record = result['targets'][1]
        assert record['owner_boundaries'] == [dict(owner=f'0x{base:08X}',
            status='inside-owner-linear-instruction')]
        assert result['targets'][2]['owners'] == [dict(start=f'0x{base:08X}', end=f'0x{base+7:08X}'),
                                                 dict(start=f'0x{base+5:08X}', end=f'0x{base+6:08X}')]
        assert result['targets_without_scan_xrefs'] == 1
        assert all(sha(p.read_bytes()) == digest for p, digest in before.items())
        assert not result['native_engine_started'] and not result['cold_boot_supported']
        # Refuse stale/tampered sources, as well as overwriting a previous audit.
        def rejected(out):
            try:
                audit(analysis, out, deps)
            except ValueError:
                return
            raise AssertionError('Invalid/overwriting audit accepted')
        rejected(output)
        for file in [stub, analysis/'fnv-analysis.xbe', scan_dir/'functions.json', scan_dir/'xrefs.json']:
            original = file.read_bytes()
            file.write_bytes(original+b' ')
            rejected(root/'must-not-exist.json')
            assert not (root/'must-not-exist.json').exists()
            file.write_bytes(original)
        # Classify non-code and holes separately, and use exclusive section ends.
        memory = dict(image_base=0x1000, image_bytes=0x3000)
        sections = [dict(start=0x1100, end=0x1200, executable=False)]
        empty = Intervals([])
        assert classify(0x1100, memory, sections, empty)[0] == 'non-executable-section'
        assert classify(0x1200, memory, sections, empty)[0] == 'image-hole'
        assert classify(0x4000, memory, sections, empty)[0] == 'outside-image'
    print('PASS 2004 overlap/oracle queries, synthetic x86 boundaries, source identity and no mutation')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--temp-dir', type=Path)
    parser.add_argument('--python-deps', type=Path)
    args = parser.parse_args()
    test(args.temp_dir, args.python_deps)

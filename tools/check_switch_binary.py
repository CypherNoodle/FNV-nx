"""Check the linked Switch artifact and its embedded NRO metadata."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--nro', type=Path, required=True)
    parser.add_argument('--nm', required=True)
    parser.add_argument('--adjustor-audit', type=Path,
                        help='Require audited guest adjustors to exist as linked weak aliases')
    parser.add_argument('--entry-audit', type=Path,
                        help='Require audited original leaf entries to exist as linked weak aliases')
    args = parser.parse_args()
    elf = args.elf.read_bytes()
    nro = args.nro.read_bytes()
    assert elf[:6] == b'\x7fELF\x02\x01', 'Expected ELF64 little endian'
    assert struct.unpack_from('<H', elf, 18)[0] == 183, 'Not AArch64'
    entry = struct.unpack_from('<Q', elf, 24)[0]
    assert entry == 0, 'Startup entry must remain at address zero'
    assert nro[16:20] == b'NRO0', 'Invalid NRO header'
    image_size = struct.unpack_from('<I', nro, 24)[0]
    assert 128 <= image_size <= len(nro), 'Invalid NRO image size'
    segments = [struct.unpack_from('<II', nro, offset) for offset in (32, 40, 48)]
    assert segments[0][0] == 0, 'NRO text must start at zero'
    previous_end = 0
    for offset, size in segments:
        assert offset >= previous_end and offset + size <= image_size, 'Invalid NRO segments'
        previous_end = offset + size
    assert nro[image_size:image_size + 4] == b'ASET', 'Missing NRO assets'
    icon_offset, icon_size, nacp_offset, nacp_size = struct.unpack_from('<QQQQ', nro, image_size + 8)
    assert icon_size and nacp_size >= 0x4000
    assert image_size + icon_offset + icon_size <= len(nro)
    assert image_size + nacp_offset + nacp_size <= len(nro)
    nacp = nro[image_size + nacp_offset:image_size + nacp_offset + nacp_size]
    title = nacp[:512].split(b'\0', 1)[0].decode('utf-8')
    author = nacp[512:768].split(b'\0', 1)[0].decode('utf-8')
    assert title == 'FNV-nx', 'Wrong hbmenu title'
    icon = nro[image_size + icon_offset:image_size + icon_offset + icon_size]
    expected_icon = Path(__file__).resolve().parents[1] / 'icon.jpg'
    assert hashlib.sha256(icon).digest() == hashlib.sha256(expected_icon.read_bytes()).digest(), 'Wrong NRO icon'
    symbols = subprocess.run([args.nm, '-n', str(args.elf)], capture_output=True, text=True, check=True).stdout
    for name in ('sub_82557648', 'sub_82244E48', 'sub_82C57A38', 'sub_82C57A48',
                 'sub_8278FC10', 'sub_825F67F8', 'sub_8224AC98',
                 'sub_827B5D98', 'sub_825FF6A8', 'sub_825F5528'):
        hook = re.search(r'^([0-9a-fA-F]+) T ' + name + r'$', symbols, re.MULTILINE)
        original = re.search(r'^([0-9a-fA-F]+) T __imp__' + name + r'$', symbols, re.MULTILINE)
        assert hook and original and hook[1] != original[1], 'Startup hook/original missing: ' + name
    adjustor_count = 0
    if args.adjustor_audit:
        audit = json.loads(args.adjustor_audit.read_text(encoding='utf-8-sig'))
        assert audit['result'] == 'PASS' and audit['generated_bodies_checked']
        for row in audit['adjustors']:
            name = 'sub_' + row['address'][2:].upper()
            alias = re.search(r'^([0-9a-fA-F]+) W ' + name + r'$', symbols, re.MULTILINE)
            body = re.search(r'^([0-9a-fA-F]+) T __imp__' + name + r'$', symbols, re.MULTILINE)
            assert alias and body and alias[1] == body[1], 'Adjustor alias/body missing: ' + name
            adjustor_count += 1
    leaf_count = 0
    if args.entry_audit:
        audit = json.loads(args.entry_audit.read_text(encoding='utf-8-sig'))
        assert audit['result'] == 'PASS' and audit['generated_bodies_checked']
        for row in audit['leaf_entries']:
            name = 'sub_' + row['address'][2:].upper()
            alias = re.search(r'^([0-9a-fA-F]+) W ' + name + r'$', symbols, re.MULTILINE)
            body = re.search(r'^([0-9a-fA-F]+) T __imp__' + name + r'$', symbols, re.MULTILINE)
            assert alias and body and alias[1] == body[1], 'Leaf alias/body missing: ' + name
            leaf_count += 1
    assert b'Mesa 26.2.4' in elf, 'Expected pinned Mesa driver fingerprint'
    for name in ('RexFnvCallProfileEnabled', 'RexFnvCallProfileAdd', 'RexFnvCallProfileReport'):
        assert re.search(r'^[0-9a-fA-F]+ T ' + name + r'$', symbols, re.MULTILINE), name
    assert b'memory-protect-inclusive' in elf and b'guest-gpu-wait-inclusive' in elf
    assert b'/switch/FNV-nx/logs/fnv-nx.log' in elf, 'Missing absolute libnx log path'
    assert b'/switch/FNV-nx/fnv-settings.ini' in elf, 'Missing INI startup path'
    for name in ('RexFnvRingProgressPublish', 'RexFnvRingProgressInvalidate',
                 'RexFnvRingProgressWait', 'RexFnvApplyThreadAffinity', 'RexFnvFpsFrame',
                 'RexFnvFpsDraw', 'RexFnvBatchSubmissionsEnabled', 'RexFnvGpuTimestampsEnabled',
                 'RexFnvDepthState', 'RexFnvLateFsiEnabled', 'RexFnvLateFsiNote',
                 'RexFnvGraphicsBegin', 'RexFnvGraphicsBarrier', 'RexFnvGraphicsState',
                 'RexFnvGraphicsReport', 'RexFnvGraphicsFrameId', 'RexFnvGraphicsImage', 'RexFnvGraphicsResolve'):
        assert re.search(r'^[0-9a-fA-F]+ T ' + name + r'$', symbols, re.MULTILINE), name
    assert b'asset_pack_build = false' in elf, 'Missing inline-folder startup defaults'
    report = {'result': 'PASS', 'architecture': 'AArch64', 'elf_entry': entry,
              'nro_bytes': len(nro), 'nro_image_bytes': image_size,
              'title': title, 'author': author, 'requested_icon_verified': True,
              'startup_hooks_and_originals_verified': True, 'mesa_version': '26.2.4',
              'call_timing_symbols_verified': True,
              'ini_optimization_extensions_linked': True,
              'audited_adjustors_linked': adjustor_count,
              'audited_leaf_entries_linked': leaf_count,
              'nro_sha256': hashlib.sha256(nro).hexdigest(),
              'hardware_tested': False, 'menu_on_switch_verified': False}
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

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
    for name in ('sub_82557648', 'sub_82244E48'):
        hook = re.search(r'^([0-9a-fA-F]+) T ' + name + r'$', symbols, re.MULTILINE)
        original = re.search(r'^([0-9a-fA-F]+) T __imp__' + name + r'$', symbols, re.MULTILINE)
        assert hook and original and hook[1] != original[1], 'Startup hook/original missing: ' + name
    assert b'Mesa 26.2.4' in elf, 'Expected pinned Mesa driver fingerprint'
    assert b'/switch/FNV-nx/logs/fnv-nx.log' in elf, 'Missing absolute libnx log path'
    assert b'asset_pack_build = false' in elf, 'Missing inline-folder startup defaults'
    report = {'result': 'PASS', 'architecture': 'AArch64', 'elf_entry': entry,
              'nro_bytes': len(nro), 'nro_image_bytes': image_size,
              'title': title, 'author': author, 'requested_icon_verified': True,
              'startup_hooks_and_originals_verified': True, 'mesa_version': '26.2.4',
              'nro_sha256': hashlib.sha256(nro).hexdigest(),
              'hardware_tested': False, 'menu_on_switch_verified': False}
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

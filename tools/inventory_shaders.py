"""Inventory private SDK Xenos dumps; never embed shader bytes in the report."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def inventory(folder):
    entries = []
    for path in sorted(folder.glob('*.ucode.bin.*')):
        match = re.fullmatch(r'shader_([0-9A-Fa-f]{16})\.ucode\.bin\.(vert|frag)', path.name)
        if not match:
            raise ValueError(f'Unexpected shader filename: {path.name}')
        data = path.read_bytes()
        if not data or len(data) % 4:
            raise ValueError(f'Invalid microcode word alignment: {path.name}')
        disassembly = path.with_name(path.name.replace('.ucode.bin.', '.ucode.'))
        text = disassembly.read_text() if disassembly.is_file() else ''
        entries.append({'id': match[1].upper(), 'stage': match[2], 'bytes': len(data),
                        'sha256': hashlib.sha256(data).hexdigest(),
                        'disassembly_present': disassembly.is_file(),
                        'fetch_formats': sorted(set(re.findall(r'DataFormat=(\w+)', text)))})
    if not entries:
        raise ValueError('No raw Xenos microcode dumps found')
    return {'unique_programs': len(entries),
            'vertex_programs': sum(e['stage'] == 'vert' for e in entries),
            'pixel_programs': sum(e['stage'] == 'frag' for e in entries),
            'programs': entries, 'shader_bytes_exported': False,
            'coverage': 'Only shaders observed in this capture; not whole-game coverage',
            'deko3d_translated': False, 'vulkan_native_library_built': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    args = parser.parse_args()
    print(json.dumps(inventory(args.folder), indent=2))


if __name__ == '__main__':
    main()

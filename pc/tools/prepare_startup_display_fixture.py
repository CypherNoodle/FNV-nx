"""Create an original, asset-free INI fixture for a private startup diagnostic.

Match dimensions to the Vulkan device prepared by the diagnostic. This does not
configure a Steam installation or establish cold startup or hardware support.
"""
import argparse
import hashlib
import json
from pathlib import Path


def prepare(output, width, height, adapter_description=None):
    if not 1 <= width <= 16384 or not 1 <= height <= 16384:
        raise ValueError('Dimensions must be in 1..16384; backend support is separate')
    if adapter_description is not None:
        if (not isinstance(adapter_description,str) or not 1<=len(adapter_description)<=255 or
            adapter_description!=adapter_description.strip() or
            any(ord(c)<32 or ord(c)>126 for c in adapter_description) or
            adapter_description.startswith('"') or adapter_description.endswith('"')):
            raise ValueError('Adapter description must round-trip as an unquoted ASCII INI value')
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    relative = Path('UserData/FNV/Documents/My Games/FalloutNV/FalloutPrefs.ini')
    path = output / relative
    path.parent.mkdir(parents=True)
    adapter_line='' if adapter_description is None else f'sD3DDevice={adapter_description}\n'
    data = (f'[Display]\niSize W={width}\niSize H={height}\nbFull Screen=1\n'+adapter_line).encode('ascii')
    path.write_bytes(data)
    report = {'schema': 'fnv-startup-display-fixture-v1', 'width': width,
              'height': height, 'windowed': False, 'file': relative.as_posix(),
              'sha256': hashlib.sha256(data).hexdigest(),
              'contains_game_assets': False, 'hardware_validated': False}
    if adapter_description is not None:report['adapter_description']=adapter_description
    (output / 'display-fixture.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--width', type=int, required=True)
    parser.add_argument('--height', type=int, required=True)
    parser.add_argument('--adapter-description',help='Exact Description returned by the selected D3D9 adapter; not its Driver field')
    args = parser.parse_args()
    print(json.dumps(prepare(args.output, args.width, args.height,args.adapter_description), indent=2))

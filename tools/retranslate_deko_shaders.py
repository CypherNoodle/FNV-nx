"""Re-translate private captured Xenos shaders for native host render targets.

Uses the SDK deferred-kill path rather than editing demote instructions. Output
still needs explicit constant/resource adaptation, UAM and hardware validation.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--translator', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    executable = str(args.translator.resolve())
    pattern = re.compile(r'shader_([0-9A-F]{16})_([0-9A-F]{16})\.spirv\.bin\.(vert|frag)')
    def translate(file):
        match = pattern.fullmatch(file.name)
        if not match:
            raise ValueError('Unrecognized shader variant filename')
        identity, modification, stage = match.groups()
        source = args.capture / f'shader_{identity}.ucode.bin.{stage}'
        destination = args.output / file.name
        result = subprocess.run([executable, str(source.resolve()), str(destination.resolve()),
                                 stage, identity, modification], capture_output=True, text=True)
        entry = {'file': file.name, 'stage': stage, 'exit_code': result.returncode,
                 'ucode_sha256': hashlib.sha256(source.read_bytes()).hexdigest()}
        if result.returncode != 0:
            entry.update(result='FAIL', diagnostic=result.stdout + result.stderr)
        else:
            entry.update(result='TRANSLATED', bytes=destination.stat().st_size,
                         translated_sha256=hashlib.sha256(destination.read_bytes()).hexdigest())
        return entry
    files = sorted(args.capture.glob('*.spirv.bin.*'))
    if not files:
        raise ValueError('No captured variants found')
    with ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(translate, files))
    print(json.dumps({'result': 'PASS' if all(r['result'] == 'TRANSLATED' for r in results) else 'FAIL',
                      'variants': len(results), 'programs': results,
                      'host_render_targets': True, 'fragment_interlock': False,
                      'sdk_deferred_pixel_kill': True, 'spirv_instruction_patching': False,
                      'hardware_verified': False}, indent=2))
    return 0 if all(r['result'] == 'TRANSLATED' for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())

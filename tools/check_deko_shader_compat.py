"""Test direct SPIR-V -> GLSL -> UAM compatibility, without rewriting shader semantics.

Inputs and translated outputs derive from the user's game and stay private.
Successful compilation would not prove resource bindings or rendered output.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shaders', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--spirv-cross', required=True)
    parser.add_argument('--uam', required=True)
    args = parser.parse_args()
    files = sorted(args.shaders.glob('*.spirv.bin.*'))
    assert files, 'No observed SPIR-V variants supplied'
    args.output.mkdir(parents=True, exist_ok=True)

    def check(file):
        results = []
        for mode, options in (('gl', []), ('vk', ['--vulkan-semantics'])):
            source = (args.output / (file.name + '.' + mode + '.glsl')).resolve()
            native = source.with_suffix('.dksh')
            cross = subprocess.run([args.spirv_cross, str(file.resolve()), '--version', '450',
                                    '--no-es', '--output', str(source), *options],
                                   capture_output=True, text=True)
            row = {'mode': mode, 'cross_exit': cross.returncode,
                   'cross_diagnostic': (cross.stdout + cross.stderr).strip()[:2000], 'uam_exit': None}
            if cross.returncode == 0:
                uam = subprocess.run([args.uam, '-s', file.suffix[1:], '-o', str(native), str(source)],
                                     capture_output=True, text=True)
                row.update(uam_exit=uam.returncode,
                           uam_diagnostic=(uam.stdout + uam.stderr).strip()[:2000])
                if uam.returncode == 0:
                    row['native_bytes'] = native.stat().st_size
            results.append(row)
        return {'file': file.name, 'sha256': hashlib.sha256(file.read_bytes()).hexdigest(),
                'attempts': results}

    with ThreadPoolExecutor(max_workers=4) as pool:
        programs = list(pool.map(check, files))
    compiled = sum(any(r['uam_exit'] == 0 for r in p['attempts']) for p in programs)
    print(json.dumps({'result': 'OBSERVATION', 'variants': len(programs),
                      'directly_compiled_variants': compiled, 'programs': programs,
                      'shader_semantics_rewritten': False, 'bindings_verified': False,
                      'deko3d_renderer_integrated': False, 'hardware_tested': False}, indent=2))


if __name__ == '__main__':
    main()

"""Build only the FNV offline adapter against unchanged external MojoShader."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build(args):
    root = args.mojoshader.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    own = Path(__file__).with_name('link_d3d9_spirv.c').resolve()
    sources = [root / 'mojoshader.c', root / 'mojoshader_common.c',
               *sorted((root / 'profiles').glob('*.c'))]
    inputs = sorted(set([*root.glob('*.h'), *root.glob('profiles/*.h'),
                         *root.glob('spirv/*.h'), *sources]))
    before = {str(p.relative_to(root)): sha(p) for p in inputs}
    (output / 'mojoshader_version.h').write_text(
        '#define MOJOSHADER_VERSION -1\n'
        '#define MOJOSHADER_CHANGESET "external-source-fingerprint-in-build-json"\n', encoding='ascii')
    exe = output / 'link_d3d9_spirv.exe'
    command = [*args.cc, '-O2', '-std=gnu99', '-I'+str(root), '-I'+str(output),
               '-DSUPPORT_PROFILE_HLSL=0', '-DSUPPORT_PROFILE_METAL=0',
               str(own), *map(str, sources), '-o', str(exe)]
    subprocess.run(command, check=True)
    after = {str(p.relative_to(root)): sha(p) for p in inputs}
    if before != after:
        raise RuntimeError('external sources changed during build')
    report = {'schema': 1, 'command': command, 'adapter_sha256': sha(own),
              'external_inputs_sha256': before, 'external_inputs_unchanged': True,
              'executable_sha256': sha(exe), 'runtime_backend': 'Vulkan',
              'game_executed': False}
    (output / 'build.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'executable': str(exe), 'external_inputs': len(before),
                      'sha256': report['executable_sha256']}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mojoshader', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--cc', nargs='+', required=True)
    build(p.parse_args())

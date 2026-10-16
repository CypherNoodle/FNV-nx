"""Build/run exact offscreen Vulkan texture/depth draws using our own fixture."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def test(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    fixture = args.fixture.resolve()
    proof = json.loads((fixture/'validation.json').read_text(encoding='utf-8'))
    assert proof['status'] == 'PASS' and proof['game_executed'] is False
    modules = [fixture/'normal.vs.spv', fixture/'normal.ps.spv']
    for module, stage in zip(modules, ('vs', 'ps')):
        assert sha(module) == proof['normal'][stage]['sha256']
    headers = args.headers.resolve()
    source = Path(__file__).with_name('vulkan_pipeline_host.c').resolve()
    exe = out/'vulkan_pipeline_host.exe'
    command = [*args.cc, '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-I'+str(headers), str(source), '-o', str(exe)]
    subprocess.run(command, check=True)
    uniform_data = getattr(args, 'uniform_data', None)
    command_run = [str(exe), *map(str, modules)]
    if uniform_data:
        command_run.append(str(uniform_data.resolve()))
    result = subprocess.run(command_run, check=True,
                            capture_output=True, text=True, timeout=60)
    driver = json.loads(result.stdout)
    assert driver['pipelines_created'] == 2 and driver['gpu_rendered'] is True
    assert driver['pixels_checked'] == 4096 and driver['draws'] == 7
    assert driver['game_executed'] is False and driver['switch_hardware_tested'] is False
    assert driver['reflected_uniform_data'] is bool(uniform_data)
    report = {'status': 'PASS', 'driver': driver, 'command': command,
              'semantic_validation': False, 'input_sha256': {
                  'source': sha(source), 'test': sha(Path(__file__)),
                  'loader': sha(Path(os.environ['SystemRoot'])/'System32/vulkan-1.dll'),
                  'fixture_validation': sha(fixture/'validation.json'),
                  'vertex_spirv': sha(modules[0]), 'pixel_spirv': sha(modules[1])},
              'header_sha256': {str(p.relative_to(headers)): sha(p)
                               for directory in ('vulkan', 'vk_video')
                               for p in sorted((headers/directory).glob('*.h'))},
              'executable_sha256': sha(exe)}
    if uniform_data:
        report['input_sha256']['uniform_data'] = sha(uniform_data)
    (out/'validation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'status': 'PASS', **report['driver']}))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('headers', 'fixture', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--cc', nargs='+', required=True)
    p.add_argument('--uniform-data', type=Path)
    test(p.parse_args())

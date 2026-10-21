"""Rebuild the native platform of a frozen private LocalAppData diagnostic.

Game and arithmetic objects are reused only with unchanged inputs and no
dependency on a modified platform header. All reports/artifacts stay private.
This diagnostic does not establish cold EXE startup or Switch execution.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess
from x87_target_evaluator import sha, unchanged


CHANGES = ('tests/captured_startup_host.c', 'native/windows_thread.c',
           'native/windows_thread.h', 'native/host_file.h',
           'native/windows_files.inc', 'native/windows_directory.inc',
           'native/windows_user32.c', 'native/windows_user32.h',
           'native/windows_events.inc')
PROVIDER = ('native/windows_shell.h', 'native/windows_shell.inc')
STEAM_PROVIDER = ('native/steam_callbacks.h', 'native/steam_callbacks.c')
PLATFORM = ('cpu_runtime.c', 'pe_image.c', 'pe_tls.c', 'windows_thread.c',
            'windows_modules.c', 'guest_memory.c', 'memory_horizon.c',
            'windows_memory.c', 'windows_user32.c', 'windows_system.c')


def permitted_changes(frozen, pc):
    allowed = {(pc / name).resolve() for name in CHANGES}
    changed = {Path(name).resolve() for name, digest in frozen.items()
               if sha(Path(name)) != digest}
    if not changed or not changed <= allowed:
        raise ValueError('Unexpected change outside the platform refresh')
    return changed


def require_current_proof(report, paths):
    if report.get('status') != 'PASS':
        raise ValueError('Require a passing regression')
    hashes = {Path(name).resolve(): digest for name, digest in report['source_sha256'].items()}
    for path in paths:
        if hashes.get(path.resolve()) != sha(path):
            raise ValueError('Regression does not cover current source: ' + str(path))


def dependency_guard(source, forced, headers, forbidden, verified=None):
    """Overapproximate quoted includes using all frozen include candidates."""
    by_name = {}
    for path in headers:
        by_name.setdefault(path.name, []).append(path)
    pending = [source, forced]
    seen = set()
    verified = verified if verified is not None else set()
    while pending:
        path = pending.pop().resolve()
        if path in forbidden:
            raise ValueError('Reused compiler unit depends on modified header: ' + str(source))
        if path in seen or path in verified:
            continue
        seen.add(path)
        text = path.read_text()
        for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', text, re.M):
            candidates = [p for p in by_name.get(Path(name).name, ()) if p.as_posix().endswith('/' + name)]
            direct = path.parent / name
            if direct.is_file() and direct.resolve() not in candidates:
                candidates.append(direct.resolve())
            if not candidates:
                raise ValueError('Unresolved quoted include in reused unit: ' + name)
            pending.extend(candidates)
    verified.update(seen)


def refresh(a):
    pc = Path(__file__).resolve().parents[1]
    native = pc / 'native'
    baseline = a.baseline.resolve()
    base = baseline.parent
    old = json.loads(baseline.read_text())
    if a.output.exists() or old.get('status') != 'EXPERIMENT_STOPPED' or not old.get('all_generated') or old.get('rejected_functions'):
        raise ValueError('Require fresh output and a complete stopped diagnostic')
    if sha(a.exe) != old['identity']['executable_sha256']:
        raise ValueError('EXE changed')
    from run_aot_analysis import sdk_identity
    sdk = a.sdk.resolve()
    if sdk_identity(sdk) != old['sdk_sha256']:
        raise ValueError('SDK changed')
    frozen = dict(old['source_sha256'])
    changed = permitted_changes(frozen, pc)
    previous = {str(p): frozen[str(p)] for p in changed}
    for path in changed:
        frozen[str(path)] = sha(path)
    provider = [pc / n for n in PROVIDER]
    steam_provider = [pc / n for n in STEAM_PROVIDER]
    require_current_proof(json.loads(a.steam_test.read_text()), steam_provider)
    for path in steam_provider:
        frozen[str(path.resolve())] = sha(path)
    for path in provider:
        frozen[str(path.resolve())] = sha(path)
    common = [native / n for n in ('windows_thread.c', 'windows_thread.h', 'windows_files.inc', 'host_file.h', 'windows_events.inc')]
    require_current_proof(json.loads(a.steam_test.read_text()), [*steam_provider,*common])
    profile_proof = json.loads(a.profile_test.read_text())
    require_current_proof(profile_proof, [*provider, *common])
    kernel_proof = json.loads(a.kernel_test.read_text())
    require_current_proof(kernel_proof, common)
    user32_proof = json.loads(a.user32_test.read_text())
    require_current_proof(user32_proof, [native / 'windows_user32.c', native / 'windows_user32.h', *common])
    cpuid = native / 'guest_cpuid.h'
    require_current_proof(json.loads(a.cpuid_test.read_text()), [cpuid, *common])
    frozen[str(cpuid.resolve())] = sha(cpuid)
    for path in (baseline, a.profile_test.resolve(), a.kernel_test.resolve(), a.user32_test.resolve(), a.cpuid_test.resolve(), a.steam_test.resolve(), Path(__file__).resolve(), a.exe.resolve()):
        frozen[str(path)] = sha(path)
    for name, digest in old['generated_files_sha256'].items():
        if Path(name).name != name:
            raise ValueError('Unsafe generated filename')
        frozen[str(base / name)] = digest
    for item in old['compiled']:
        frozen[str(base / item['object'])] = item['object_sha256']
    for item in old['host_x87_objects']:
        frozen[item['object']] = item['sha256']
    frozen[str(base / 'startup-test.exe')] = old['executable_sha256']
    rows = (base / 'link.rsp').read_text().splitlines()
    if any(not (row.startswith('"') and row.endswith('"')) for row in rows):
        raise ValueError('Unexpected link response')
    args = [row[1:-1] for row in rows]
    if args[-2:] != ['-o', (base / 'startup-test.exe').as_posix()]:
        raise ValueError('Unexpected link output')
    objects = [Path(name) for name in args[:-2]]
    if len(set(objects)) != len(objects) or any(str(p) not in frozen for p in objects):
        raise ValueError('Unfrozen/duplicate linked object')
    sources = [*[native / name for name in PLATFORM], pc / 'tests/captured_startup_host.c']
    replace = {p.stem: p for p in sources}
    if any(sum(obj.stem == name for obj in objects) != 1 for name in replace):
        raise ValueError('Ambiguous native compiler unit')
    # This provider is new, not a replacement for any frozen game object.
    if any(obj.stem=='steam_callbacks' for obj in objects):
        raise ValueError('Steam callback provider already linked in baseline')
    sources.append(native / 'steam_callbacks.c')
    headers = {Path(name).resolve() for name in frozen if Path(name).suffix in ('.h', '.inc')}
    forbidden = {p for p in changed if p.suffix in ('.h', '.inc')}
    reused = [p for p in objects if p.stem not in replace]
    verified_dependencies = set()
    for obj in reused:
        source = obj.with_suffix('.c')
        if not source.is_file() or str(source) not in frozen:
            matches = [Path(name) for name in frozen if Path(name).name == obj.stem + '.c']
            if len(matches) != 1:
                raise ValueError('Cannot prove reused object source: ' + str(obj))
            source = matches[0]
        dependency_guard(source, native / 'fnv_sdk_runtime.h', headers, forbidden, verified_dependencies)
    unchanged(frozen)
    core = next((Path(p) for p, h in frozen.items() if h == old['host_x87']['core_validation_sha256']), None)
    if core is None:
        raise ValueError('Missing frozen arithmetic compiler identity')
    cc = json.loads(core.read_text())['compiler']
    out = a.output.resolve()
    out.mkdir(parents=True)
    def run(command, name):
        result = subprocess.run([str(v) for v in command], capture_output=True, text=True, timeout=120)
        (out / name).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(f'{name}: status {result.returncode}: {result.stderr[-4000:]}')
        return result.stdout
    header = [Path(p) for p in frozen if Path(p).name == 'generated_functions.h']
    if len(header) != 1:
        raise ValueError('Ambiguous generated header')
    includes = ['-I' + str(p) for p in (header[0].parent, native, sdk / 'templates/runtime', sdk / 'src/kernel')]
    compiled = []
    for source in sources:
        obj = out / (source.stem + '.o')
        run([*cc, *old['compiler_flags'], *includes, '-include', native / 'fnv_sdk_runtime.h', '-c', source, '-o', obj], source.stem + '.log')
        compiled.append(dict(source=source.name, object=obj.name, object_sha256=sha(obj)))
    from probe_captured_startup import object_response
    rsp = out / 'link.rsp'
    program = out / 'startup-test.exe'
    run([*cc, object_response(rsp, [*reused, *[out / item['object'] for item in compiled]], program)], 'link.log')
    blob = out / 'captured-text-data.bin'
    shutil.copyfile(base / 'captured-text-data.bin', blob)
    from startup_memory_profile import configuration, validate_observation
    memory = configuration(old['memory_profile'])
    root = a.file_root.resolve()
    if not root.is_dir():
        raise ValueError('File root must already exist')
    host_args = [program, a.exe.resolve(), root, blob,
                 *[str(memory[k]) for k in ('arena_bytes', 'vm_bytes', 'commit_limit')], '',
                 str(old['host']['worker_stats']['capacity'])]
    host = json.loads(run(host_args, 'host.log').strip().splitlines()[-1])
    validate_observation(host, old['memory_profile'])
    unchanged(frozen)
    report = {**old, 'status': 'EXPERIMENT_STOPPED' if host['faulted'] else 'EXPERIMENT_RETURNED',
              'host': host, 'source_sha256': frozen, 'compiled': compiled,
              'executable_sha256': sha(program), 'generated_files_sha256': {p.name: sha(p) for p in (rsp, blob)},
              'refreshed_platform_sources': dict(previous=previous, current={str(p): sha(p) for p in changed}),
              'new_native_provider_sources': {str(p): sha(p) for p in [*provider,*steam_provider]},
              'reused_link_objects': len(reused), 'diagnostic_file_root': str(root),
              'suitable_for_switch_packaging': False, 'native_engine_started': False,
              'cold_boot_supported': False, 'switch_hardware_tested': False}
    (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(dict(status=report['status'], calls=host['call_count'], stop=host['text'], rebuilt=len(compiled), reused=len(reused))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'exe', 'sdk', 'output', 'profile-test', 'kernel-test', 'user32-test', 'cpuid-test', 'steam-test', 'file-root'):
        parser.add_argument('--' + name, type=Path, required=True)
    refresh(parser.parse_args())

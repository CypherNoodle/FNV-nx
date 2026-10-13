"""Compile private generated Xbox code with devkitA64 and Horizon headers.

This validates compiler/ABI compatibility, not runtime execution or graphics.
All logs and ARM objects stay under ignored local/switch-cpu.
"""
from pathlib import Path
import argparse
import concurrent.futures
import json
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated-dir', type=Path, required=True)
    parser.add_argument('--horizon-sdk', type=Path, required=True)
    parser.add_argument('--dependency-include', type=Path, required=True)
    parser.add_argument('--devkitpro', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--partitions', help='Comma-separated partition numbers; default: all')
    parser.add_argument('--support-only', action='store_true', help='Compile generated init/registration units only')
    parser.add_argument('--partial-link', action='store_true', help='Combine all objects and check guest function references')
    args = parser.parse_args()
    if not 1 <= args.jobs <= 16:
        parser.error('--jobs must be between 1 and 16')
    root = Path(__file__).resolve().parents[1]
    generated = args.generated_dir.resolve()
    sdk = args.horizon_sdk.resolve()
    deps = args.dependency_include.resolve()
    devkit = args.devkitpro.resolve()
    compiler = devkit / 'devkitA64/bin/aarch64-none-elf-g++'
    if not compiler.exists():
        compiler = compiler.with_suffix('.exe')
    for path in (compiler, generated / 'fnv_xbox_pch.h', sdk / 'include/rex/platform.h', deps / 'simde'):
        if not path.exists():
            parser.error(f'Missing prerequisite: {path}')
    sources = sorted(generated.glob('fnv_xbox_recomp.*.cpp'))
    if args.support_only and args.partitions:
        parser.error('--support-only and --partitions are mutually exclusive')
    if args.partial_link and (args.support_only or args.partitions):
        parser.error('--partial-link requires all translation units')
    if args.partitions:
        selected = {int(item) for item in args.partitions.split(',')}
        sources = [p for p in sources if int(p.name.split('.')[-2]) in selected]
        if len(sources) != len(selected):
            parser.error('Some requested partitions are missing')
    if args.support_only:
        sources = [generated / 'fnv_xbox_init.cpp', generated / 'fnv_xbox_register.cpp']
    elif not args.partitions:
        sources += [generated / 'fnv_xbox_init.cpp', generated / 'fnv_xbox_register.cpp']
    if not sources:
        parser.error('No generated partitions found')
    output = root / 'local/switch-cpu'
    output.mkdir(parents=True, exist_ok=True)
    version = subprocess.run([str(compiler), '--version'], capture_output=True, text=True, check=True).stdout.splitlines()[0]
    nm = compiler.with_name('aarch64-none-elf-nm' + compiler.suffix)
    common = [str(compiler), '-std=gnu++23', '-O0', '-ffp-contract=off', '-fno-strict-aliasing',
              '-march=armv8-a+crc+crypto', '-mtune=cortex-a57', '-mtp=soft',
              '-ftls-model=local-exec', '-fPIE', '-D__SWITCH__', '-DNX', '-D_GNU_SOURCE',
              '-DSPDLOG_NO_TZ_OFFSET', '-DSPDLOG_FMT_EXTERNAL', '-Wno-psabi', '-fmax-errors=5',
              '-include', str(root / 'src/switch_codegen_compat.h')]
    for folder in (sdk / 'switch_compat', sdk / 'include', deps,
                   devkit / 'libnx/include', devkit / 'portlibs/switch/include', generated):
        common += ['-I', str(folder)]

    def compile_partition(source):
        start = time.monotonic()
        obj = output / (source.stem + '.o')
        with (output / (source.stem + '.log')).open('w', encoding='utf-8') as log:
            result = subprocess.run(common + ['-c', str(source), '-o', str(obj)], stdout=log, stderr=subprocess.STDOUT)
        aliases = 0
        alias_errors = []
        if result.returncode == 0:
            symbols = subprocess.run([str(nm), str(obj)], capture_output=True, text=True, check=True).stdout
            defined = {}
            for line in symbols.splitlines():
                fields = line.split()
                if len(fields) == 3:
                    defined[fields[2]] = (fields[0], fields[1])
            for name, (address, kind) in defined.items():
                if kind == 'T' and name.startswith('__imp__'):
                    alias = defined.get(name[len('__imp__'):])
                    if alias != (address, 'W'):
                        alias_errors.append(name)
                    else:
                        aliases += 1
        return {'partition': source.name, 'exit_code': result.returncode,
                'seconds': round(time.monotonic() - start, 3),
                'weak_aliases_verified': aliases, 'alias_errors': alias_errors,
                'object_bytes': obj.stat().st_size if result.returncode == 0 else None}

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for result in pool.map(compile_partition, sources):
            results.append(result)
            valid = result['exit_code'] == 0 and not result['alias_errors']
            print(result['partition'], 'PASS' if valid else 'FAIL', flush=True)
    report = {'compiler': version, 'target': 'Nintendo Switch Cortex-A57/libnx',
              'result': 'PASS' if all(r['exit_code'] == 0 and not r['alias_errors'] for r in results) else 'FAIL',
              'tested_units': len(results), 'total_units': len(list(generated.glob('*.cpp'))),
              'weak_aliases_verified': sum(r['weak_aliases_verified'] for r in results),
              'runtime_tested': False, 'nro_linked': False, 'results': results}
    if args.partial_link and report['result'] == 'PASS':
        link_report = partial_link(compiler, output, [output / (p.stem + '.o') for p in sources])
        report['partial_link'] = link_report
        if link_report['result'] != 'PASS':
            report['result'] = 'FAIL'
    report_name = 'support-report.json' if args.support_only else 'report.json'
    (output / report_name).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return 0 if report['result'] == 'PASS' else 1


def partial_link(compiler, output, objects):
    linker = compiler.with_name('aarch64-none-elf-ld' + compiler.suffix)
    nm = compiler.with_name('aarch64-none-elf-nm' + compiler.suffix)
    response = output / 'objects.rsp'
    response.write_text(''.join('"' + p.as_posix() + '"\n' for p in objects), encoding='utf-8')
    linked = output / 'fnv-recompiled.o'
    with (output / 'partial-link.log').open('w', encoding='utf-8') as log:
        result = subprocess.run([str(linker), '-r', '--no-relax', '-o', str(linked), '@' + str(response)], stdout=log, stderr=subprocess.STDOUT)
    unresolved = []
    if result.returncode == 0:
        symbols = subprocess.run([str(nm), '-u', str(linked)], capture_output=True, text=True, check=True).stdout
        unresolved = [line.strip() for line in symbols.splitlines() if re.search(r'\b(?:__imp__)?sub_[0-9A-Fa-f]+$', line)]
    return {'result': 'PASS' if result.returncode == 0 and not unresolved else 'FAIL',
            'link_exit_code': result.returncode, 'unresolved_guest_functions': unresolved,
            'object_count': len(objects), 'runtime_linked': False, 'nro_linked': False}


if __name__ == '__main__':
    raise SystemExit(main())

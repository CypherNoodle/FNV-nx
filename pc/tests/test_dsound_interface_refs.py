"""Record native DirectSound public reference counts without playing audio."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from test_windows_heap import compiler_environment


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc', nargs='+', required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = Path(__file__).with_name('dsound_interface_refs_host.c').resolve()
    exe = out / 'native-interface-refs.exe'
    command = [*a.cc, '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
               str(source), '-ldsound', '-luser32', '-o', str(exe)]
    env = compiler_environment(a.cc)
    build = subprocess.run(command, capture_output=True, text=True, env=env,
                           timeout=120)
    (out / 'build.log').write_text(build.stdout + build.stderr, newline='\n')
    build.check_returncode()
    run = subprocess.run([str(exe)], capture_output=True, text=True, env=env,
                         timeout=30)
    (out / 'run.log').write_text(run.stdout + run.stderr, newline='\n')
    run.check_returncode()
    result = json.loads(run.stdout)
    assert result['status'] == 'PASS' and result['played_audio'] is False
    result.update(command=command, source_sha256={
        str(path): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in (source, Path(__file__).resolve())},
        exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
        switch_hardware_tested=False)
    (out / 'validation.json').write_text(json.dumps(result, indent=2) + '\n',
                                        newline='\n')
    print(json.dumps(result))


if __name__ == '__main__':
    main()

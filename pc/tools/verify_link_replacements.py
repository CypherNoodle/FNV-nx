"""Check that a response file links each exact replacement object once.

A compiled replacement beside a response file does not establish that it was
linked. This check records paths and hashes without executing private code.
"""
import argparse
import hashlib
import json
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(response, replacements):
    lines = response.read_text().splitlines()
    if not lines or any(not (line.startswith('"') and line.endswith('"')) for line in lines):
        raise ValueError('Expected one quoted linker argument per line')
    args = [line[1:-1] for line in lines]
    if args[-2:-1] != ['-o'] or not args[-1]:
        raise ValueError('Expected explicit output as final linker arguments')
    # Resolve relative objects as the linker does: relative to caller cwd.
    objects = [Path(a).resolve() for a in args[:-2] if Path(a).suffix.lower() in ('.o', '.obj')]
    if len(objects) != len(set(objects)):
        raise ValueError('Duplicate object paths in link response')
    wanted = [p.resolve() for p in replacements]
    if not wanted or len(wanted) != len(set(wanted)):
        raise ValueError('Require unique explicit replacement paths')
    report = []
    for obj in wanted:
        if not obj.is_file() or obj.suffix.lower() not in ('.o', '.obj'):
            raise ValueError('Replacement object missing or unsupported')
        same_name = [p for p in objects if p.name.casefold() == obj.name.casefold()]
        if same_name != [obj]:
            raise ValueError('Missing, stale or ambiguous replacement: '+obj.name)
        report.append(dict(object=str(obj), sha256=sha(obj)))
    return dict(status='LINK_REPLACEMENTS_VERIFIED', response_sha256=sha(response),
                replacements=report, output=args[-1],
                limitation='Verifies response inputs only. Does not prove a successful link, execution, or game startup.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--response', type=Path, required=True)
    parser.add_argument('--replacement', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    a = parser.parse_args()
    result = verify(a.response, a.replacement)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('x', newline='\n') as f:
        json.dump(result, f, indent=2)
        f.write('\n')
    print(result['status'], len(result['replacements']))


if __name__ == '__main__':
    main()

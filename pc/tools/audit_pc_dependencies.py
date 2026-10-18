"""Read-only normal-import graph of locally supplied PC game EXE/DLLs.

Presence is not proof that a library is needed at runtime. Dynamic loading,
delay imports, exports, DRM and executable compatibility are not established.
Only metadata is emitted; no binaries or file contents are copied.
"""
import argparse
import json
from pathlib import Path
from audit_install import pe_info


def audit(game):
    game = Path(game)
    files = {p.name.lower(): p for p in game.iterdir()
             if p.is_file() and p.suffix.lower() == '.dll'}
    files['falloutnv.exe'] = game / 'FalloutNV.exe'
    parsed = {name: pe_info(path) for name, path in sorted(files.items())}
    reachable, pending = set(), ['falloutnv.exe']
    while pending:
        name = pending.pop()
        if name in reachable:
            continue
        reachable.add(name)
        pending.extend(item['dll'].lower() for item in parsed[name]['imports']
                       if item['dll'].lower() in parsed)
    return dict(normal_imports_only=True, native_engine_started=False,
                libraries=[dict(name=files[name].name, sha256=pe['sha256'],
                                bytes=pe['bytes'], normal_import_reachable=name in reachable,
                                imports=pe['imports']) for name, pe in parsed.items()],
                limits=['Dynamic and delay imports are not traversed',
                        'A library not reachable here may still be loaded dynamically',
                        'Imported APIs still need native bridges or translated implementations'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = audit(args.game)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    for item in report['libraries']:
        print(item['name'], 'normal-import reachable:', item['normal_import_reachable'],
              'imports:', sum(len(d['symbols']) for d in item['imports']))

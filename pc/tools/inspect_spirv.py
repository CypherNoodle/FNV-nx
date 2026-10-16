"""Bounded SPIR-V framing/reflection inspector; NOT a semantic validator."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def inspect(data):
    if len(data) < 20 or len(data) > 4*1024*1024 or len(data) % 4:
        raise ValueError('invalid module size')
    w = struct.unpack('<%dI' % (len(data)//4), data)
    if w[0] != 0x07230203 or w[4] != 0 or not 0 < w[3] <= 1_000_000:
        raise ValueError('invalid SPIR-V header')
    names, decorations, variables, entries = {}, {}, {}, []
    count, pos = 0, 5

    def string(words):
        raw = struct.pack('<%dI' % len(words), *words)
        end = raw.find(b'\0')
        if end < 0:
            raise ValueError('unterminated SPIR-V string')
        return raw[:end].decode('utf-8'), (end//4)+1

    def ident(value):
        if not 0 < value < w[3]:
            raise ValueError('id outside declared bound')
        return value

    while pos < len(w):
        length, op = w[pos] >> 16, w[pos] & 0xffff
        if not length or pos + length > len(w):
            raise ValueError('truncated/zero-length instruction or auxiliary trailer')
        a = w[pos+1:pos+length]
        if op == 5:  # OpName
            if len(a) < 2:
                raise ValueError('short OpName')
            names[ident(a[0])] = string(a[1:])[0]
        elif op == 15:  # OpEntryPoint
            if len(a) < 3:
                raise ValueError('short OpEntryPoint')
            name, used = string(a[2:])
            entries.append({'model': a[0], 'id': ident(a[1]), 'name': name,
                            'interface': [ident(i) for i in a[2+used:]]})
        elif op == 71:  # OpDecorate
            if len(a) < 2:
                raise ValueError('short OpDecorate')
            decorations.setdefault(ident(a[0]), []).append(list(a[1:]))
        elif op == 59:  # OpVariable
            if len(a) not in (3, 4):
                raise ValueError('invalid OpVariable')
            variables[ident(a[1])] = {'type_id': ident(a[0]), 'storage': a[2]}
        count += 1
        pos += length
    if len(entries) != 1 or entries[0]['name'] != 'main' or entries[0]['model'] not in (0, 4):
        raise ValueError('expected one vertex or fragment main entry')
    resources, interfaces = [], []
    for identifier, var in variables.items():
        item = {'id': identifier, 'name': names.get(identifier), **var}
        for d in decorations.get(identifier, []):
            if d[0] in (14, 16, 17):
                flag = {14: 'flat', 16: 'centroid', 17: 'sample'}[d[0]]
                if len(d) != 1 or flag in item:
                    raise ValueError('invalid/duplicate interpolation decoration')
                item[flag] = True
            key = {11: 'builtin', 30: 'location', 33: 'binding', 34: 'set'}.get(d[0])
            if key:
                if len(d) != 2 or key in item:
                    raise ValueError('invalid/duplicate interface decoration')
                item[key] = d[1]
        if var['storage'] in (0, 2):
            if 'set' not in item or 'binding' not in item:
                raise ValueError('resource lacks explicit descriptor set/binding')
            resources.append(item)
        if var['storage'] in (1, 3):
            interfaces.append(item)
    return {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
            'version': w[1], 'bound': w[3], 'instructions': count,
            'entry': entries[0], 'resources': resources, 'interfaces': interfaces,
            'framing_checked': True, 'semantic_validation': False}


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('module', type=Path)
    args = p.parse_args()
    print(json.dumps(inspect(args.module.read_bytes()), indent=2))

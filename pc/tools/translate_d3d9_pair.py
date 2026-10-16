"""Preserve D3D9 centroid declarations across an unchanged external translator.

Only the temporary SM2/3 DCL destination bit is cleared. After pair linking,
Centroid is attached to the corresponding fragment Input, never a Private
copy or builtin. Unknown mappings fail. Original bytecode is never modified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from inspect_spirv import inspect

CENTROID = 0x00400000


def digest(data):
    return hashlib.sha256(data).hexdigest()


def normalize(data):
    if not 8 <= len(data) <= 1024*1024 or len(data) % 4:
        raise ValueError('invalid D3D9 bytecode size')
    words = list(struct.unpack('<%dI' % (len(data)//4), data))
    stage, major = words[0] >> 16, (words[0] >> 8) & 255
    if stage not in (0xfffe, 0xffff):
        raise ValueError('invalid D3D9 stage')
    if major == 1:
        return data, []  # No centroid in SM1; delegate unchanged to translator.
    if major not in (2, 3):
        raise ValueError('unsupported D3D9 shader model')
    declarations, seen, pos = [], set(), 1
    while pos < len(words):
        token = words[pos]
        opcode = token & 0xffff
        if opcode == 0xffff:
            if token != 0xffff or pos != len(words)-1:
                raise ValueError('invalid END or trailing bytecode')
            return struct.pack('<%dI' % len(words), *words), declarations
        length = ((token >> 16) & 0x7fff) if opcode == 0xfffe else ((token >> 24) & 15)
        if pos+1+length > len(words):
            raise ValueError('truncated D3D9 instruction/comment')
        if opcode == 31:  # DCL semantic, destination
            if length != 2 or token & (1 << 28):
                raise ValueError('invalid/predicated DCL')
            dest, semantic = words[pos+2], words[pos+1]
            kind = ((dest >> 28) & 7) | ((dest >> 8) & 24)
            reg = dest & 2047
            if dest & CENTROID:
                if stage != 0xffff or kind not in (1, 3) or reg >= 16:
                    raise ValueError('centroid requires bounded pixel input')
                if dest & 0x0000e000 or not dest & 0x80000000 or not dest & 0x000f0000:
                    raise ValueError('invalid centroid destination flags/mask')
                if major == 3:
                    if kind != 1:
                        raise ValueError('SM3 centroid requires input register')
                    usage, index = semantic & 15, (semantic >> 16) & 15
                else:
                    usage, index = (10 if kind == 1 else 5), reg
                if usage not in (5, 10) or (kind, reg) in seen:
                    raise ValueError('unsupported/duplicate centroid semantic')
                seen.add((kind, reg))
                name = 'ps_PointCoordOrTexCoord0' if (usage, index) == (5, 0) else 'ps_%s%d' % ('v' if kind == 1 else 't', reg)
                declarations.append({'token_word': pos+2, 'register_type': kind,
                                     'register': reg, 'usage': usage, 'index': index,
                                     'spirv_name': name})
                words[pos+2] = dest & ~CENTROID
        pos += 1+length
    raise ValueError('missing D3D9 END')


def restore(data, declarations):
    reflection = inspect(data)
    if not declarations:
        return data, []
    if reflection['entry']['model'] != 4:
        raise ValueError('centroid requires fragment SPIR-V')
    selected = []
    for declaration in declarations:
        candidates = [v for v in reflection['interfaces']
                      if v['name'] == declaration['spirv_name'] and v['storage'] == 1
                      and v['id'] in reflection['entry']['interface']
                      and 'builtin' not in v and v.get('location', 0xffffffff) != 0xffffffff]
        if len(candidates) != 1:
            raise ValueError('centroid input mapping absent or ambiguous')
        target = candidates[0]
        if target.get('flat') or target.get('sample') or target.get('centroid') or target['id'] in selected:
            raise ValueError('conflicting interpolation decoration')
        selected.append(target['id'])
    words = list(struct.unpack('<%dI' % (len(data)//4), data))
    pos = 5
    while pos < len(words):
        length, opcode = words[pos] >> 16, words[pos] & 0xffff
        if opcode == 19:  # OpTypeVoid: annotations must precede types.
            break
        pos += length
    if pos == len(words):
        raise ValueError('missing SPIR-V type section')
    annotations = [word for identifier in selected for word in ((3 << 16) | 71, identifier, 16)]
    words[pos:pos] = annotations
    result = struct.pack('<%dI' % len(words), *words)
    verified = inspect(result)
    if {v['id'] for v in verified['interfaces'] if v.get('centroid')} != set(selected):
        raise ValueError('centroid restoration failed')
    return result, selected


def translate(args):
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    records = {}
    for stage, source in [('vs', args.vs), ('ps', args.ps)]:
        original = source.read_bytes()
        normalized, declarations = normalize(original)
        (out/(stage+'.normalized.bin')).write_bytes(normalized)
        records[stage] = {'original_sha256': digest(original), 'normalized_sha256': digest(normalized),
                          'centroid': declarations}
    r = subprocess.run([str(args.linker.resolve()), str(out/'vs.normalized.bin'),
                        str(out/'ps.normalized.bin'), str(args.attributes.resolve()),
                        str(out/'vs.linked.spv'), str(out/'ps.linked.spv')],
                       capture_output=True, text=True, timeout=30)
    (out/'linker.stderr.txt').write_text(r.stderr, encoding='utf-8')
    if r.returncode:
        raise RuntimeError('pair link rejected: '+r.stderr)
    for stage in ('vs', 'ps'):
        final, targets = restore((out/(stage+'.linked.spv')).read_bytes(), records[stage]['centroid'])
        (out/(stage+'.spv')).write_bytes(final)
        records[stage].update(spirv=inspect(final), centroid_ids=targets)
    report = {'status': 'PASS', 'stages': records, 'link': json.loads(r.stdout),
              'linker_sha256': digest(args.linker.read_bytes()),
              'tool_sha256': digest(Path(__file__).read_bytes()),
              'inspector_sha256': digest(Path(inspect.__code__.co_filename).read_bytes()),
              'attributes_sha256': digest(args.attributes.read_bytes()),
              'gpu_rendered': False, 'semantic_validation': False, 'game_executed': False}
    (out/'translation.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('vs', 'ps', 'attributes', 'linker', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    report = translate(p.parse_args())
    print(json.dumps({'status': report['status'], 'centroid_inputs': len(report['stages']['ps']['centroid_ids'])}))

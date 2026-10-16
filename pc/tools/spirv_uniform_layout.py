"""Reflect the examined MojoShader Vulkan UBO ABI into bounded upload spans.

Uses public translator register ranges AND actual SPIR-V member/array layouts
and named integer slot constants. Does not infer packing from metadata order.
Shader-local constants are excluded. Unknown layouts fail explicitly.
"""
import struct
from inspect_spirv import inspect


def reflect(data, uniforms):
    summary = inspect(data)
    words = struct.unpack('<%dI' % (len(data)//4), data)
    types, constants, names, decorations, members = {}, {}, {}, {}, {}
    pos = 5
    while pos < len(words):
        length, op = words[pos] >> 16, words[pos] & 65535
        a = words[pos+1:pos+length]
        if op == 5:
            names[a[0]] = struct.pack('<%dI' % (len(a)-1), *a[1:]).split(b'\0')[0].decode('utf-8')
        elif op in (21, 22, 23, 28, 30, 32):
            types[a[0]] = (op, a[1:])
        elif op == 43 and len(a) == 3:
            constants[a[1]] = (a[0], a[2])
        elif op == 71:
            decorations.setdefault(a[0], []).append(tuple(a[1:]))
        elif op == 72:
            members.setdefault((a[0], a[1]), []).append(tuple(a[2:]))
        pos += length

    def decoration(table, key, tag):
        matches = [x[1:] for x in table.get(key, []) if x[0] == tag]
        if len(matches) != 1 or len(matches[0]) != 1:
            raise ValueError('missing/ambiguous SPIR-V layout decoration')
        return matches[0][0]

    def literal(identifier):
        scalar, value = constants[identifier]
        if types[scalar] not in ((21, (32, 1)), (21, (32, 0))):
            raise ValueError('expected 32-bit integer layout literal')
        return value

    active = [u for u in uniforms if not u['local']]
    ubos = [r for r in summary['resources'] if r['storage'] == 2]
    if not ubos and not active:
        return {'bytes': 0, 'entries': [], 'set': None, 'binding': None}
    if len(ubos) != 1:
        raise ValueError('expected one explicit register-bank UBO')
    ubo = ubos[0]
    pointer = types[ubo['type_id']]
    if pointer[0] != 32 or pointer[1][0] != 2:
        raise ValueError('unexpected uniform pointer')
    sid = pointer[1][1]
    if (2,) not in decorations.get(sid, []) or types[sid][0] != 30:
        raise ValueError('expected uniform Block structure')
    arrays, end, occupied = {}, 0, set()
    for index, array_id in enumerate(types[sid][1]):
        array = types[array_id]
        if array[0] != 28:
            raise ValueError('expected uniform array member')
        element, length_id = array[1]
        element_type = types[element]
        if element_type[0] == 23 and element_type[1][1] == 4:
            scalar = types[element_type[1][0]]
            kind = 0 if scalar == (22, (32,)) else 1 if scalar == (21, (32, 1)) else -1
        else:
            kind = 2 if element_type == (21, (32, 1)) else -1
        if kind < 0 or kind in arrays:
            raise ValueError('unknown/duplicate uniform bank member')
        count = literal(length_id)
        offset = decoration(members, (sid, index), 35)
        stride = decoration(decorations, array_id, 6)
        if stride != 16 or offset % 16 or not 0 < count <= (256 if kind == 0 else 16):
            raise ValueError('unsupported uniform bank size/stride/alignment')
        slots = set(range(offset//16, offset//16+count))
        if slots & occupied:
            raise ValueError('overlapping uniform members')
        occupied |= slots
        end = max(end, offset+count*16)
        arrays[kind] = {'count': count, 'offset': offset}
    if end > 65536:
        raise ValueError('uniform block exceeds supported capacity')
    named_slots = {}
    for identifier, name in names.items():
        if identifier in constants and types[constants[identifier][0]] == (21, (32, 1)):
            named_slots.setdefault(name, []).append(literal(identifier))
    entries, covered, source = [], set(), set()
    for uniform in active:
        kind, first, count = uniform['kind'], uniform['first'], uniform['count']
        if kind not in arrays or count <= 0 or first < 0 or first+count > (256 if kind == 0 else 16):
            raise ValueError('invalid reflected API register range')
        candidates = [slot for reg in range(first, first+count)
                      for slot in named_slots.get('cib'[kind]+str(reg), [])]
        if not candidates or len(set(candidates)) != 1:
            raise ValueError('missing/ambiguous register-to-packed-slot mapping')
        start = candidates[0]
        array = arrays[kind]
        if start+count > array['count']:
            raise ValueError('uniform span outside reflected array')
        offset = array['offset']+start*16
        slots = set(range(offset//16, offset//16+count))
        registers = {(kind, reg) for reg in range(first, first+count)}
        if slots & covered or registers & source:
            raise ValueError('overlapping upload ranges')
        source |= registers
        covered |= slots
        entries.append({'kind': kind, 'first': first, 'count': count, 'offset': offset, 'stride': 16})
    if covered != occupied:
        raise ValueError('uniform block contains unmapped slots')
    return {'bytes': end, 'entries': sorted(entries, key=lambda e: e['offset']),
            'set': ubo['set'], 'binding': ubo['binding']}

"""Experimental native constant ABI and UAM modules from private captured shaders.

Changes only UBO member offsets in a private SPIR-V copy. Emits an explicit
bit-copy plan for the new ABI. Shader/resource correctness needs GPU validation;
this is not a native game renderer or a performance measurement.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


def align(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def layout(reflection):
    offsets, blocks = {}, []
    for block in reflection.get('ubos', []):
        cursor, moves = 0, []
        type_id = int(block['type'][1:])
        for index, member in enumerate(reflection['types'][block['type']]['members']):
            kind = member['type']
            scalar = kind in ('float', 'int', 'uint', 'bool')
            vector = re.fullmatch(r'[uib]?vec([234])', kind)
            if not scalar and not vector:
                raise ValueError('Unsupported native constant type: ' + kind)
            components = 1 if scalar else int(vector[1])
            size = components * 4
            alignment = 4 if components == 1 else 8 if components == 2 else 16
            arrays = member.get('array', [])
            if arrays:
                if len(arrays) != 1 or arrays[0] <= 0 or not all(member['array_size_is_literal']):
                    raise ValueError('Dynamic or multidimensional UBO array requires explicit support')
                count = arrays[0]
                alignment = max(16, alignment)
                stride = align(size, 16)
                old_stride = member['array_stride']
            else:
                count, stride, old_stride = 1, size, size
            cursor = align(cursor, alignment)
            offsets[(type_id, index)] = cursor
            moves.append({'name': member['name'], 'source_offset': member['offset'],
                          'native_offset': cursor, 'element_bytes': size, 'elements': count,
                          'source_stride': old_stride, 'native_stride': stride})
            cursor += count * stride
        blocks.append({'name': block['name'], 'set': block['set'], 'binding': block['binding'],
                       'source_bytes': block['block_size'], 'native_bytes': align(cursor, 16),
                       'fields': moves})
    return offsets, blocks


def verify_copy_plan(block):
    # Distinct bytes exercise every field/element and all offsets, not just zeros.
    source = bytes((i * 73 + i // 256) % 256 for i in range(block['source_bytes']))
    target = bytearray(block['native_bytes'])
    occupied = set()
    for field in block['fields']:
        for element in range(field['elements']):
            s = field['source_offset'] + element * field['source_stride']
            d = field['native_offset'] + element * field['native_stride']
            n = field['element_bytes']
            assert s + n <= len(source) and d + n <= len(target)
            positions = set(range(d, d + n))
            assert not occupied.intersection(positions), 'Overlapping native fields'
            occupied.update(positions)
            target[d:d + n] = source[s:s + n]
    for field in block['fields']:
        for element in range(field['elements']):
            s = field['source_offset'] + element * field['source_stride']
            d = field['native_offset'] + element * field['native_stride']
            n = field['element_bytes']
            assert target[d:d + n] == source[s:s + n]


def run(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise ValueError((result.stdout + result.stderr).strip()[:3000])
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shaders', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--spirv-cross', required=True)
    parser.add_argument('--spirv-val', required=True)
    parser.add_argument('--uam', required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    inputs = sorted(args.shaders.glob('*.spirv.bin.*'))
    assert inputs, 'No captured SPIR-V variants'

    def build(file):
        item = {'file': file.name, 'source_sha256': hashlib.sha256(file.read_bytes()).hexdigest()}
        try:
            reflection = json.loads(run([args.spirv_cross, str(file.resolve()), '--reflect']))
            offsets, blocks = layout(reflection)
            for block in blocks:
                verify_copy_plan(block)
            binary = file.read_bytes()
            words = list(struct.unpack('<' + 'I' * (len(binary) // 4), binary))
            cursor, changed = 5, set()
            while cursor < len(words):
                length, opcode = words[cursor] >> 16, words[cursor] & 0xFFFF
                assert length and cursor + length <= len(words)
                if opcode == 72 and length == 5 and words[cursor + 3] == 35:
                    key = (words[cursor + 1], words[cursor + 2])
                    if key in offsets:
                        words[cursor + 4] = offsets[key]
                        changed.add(key)
                cursor += length
            assert changed == offsets.keys(), 'Not all native offsets were written'
            prefix = args.output / file.name
            adapted = Path(str(prefix) + '.std140.spv').resolve()
            glsl = Path(str(prefix) + '.glsl').resolve()
            native = Path(str(prefix) + '.dksh').resolve()
            adapted.write_bytes(struct.pack('<' + 'I' * len(words), *words))
            run([args.spirv_val, '--target-env', 'vulkan1.2', str(adapted)])
            run([args.spirv_cross, str(adapted), '--version', '450', '--no-es', '--output', str(glsl)])
            # UAM needs explicit combined sampler bindings. Preserve the original
            # image/sampler pair in the ABI manifest for the future resource binder.
            pairs = {}
            for image in reflection.get('separate_images', []):
                for sampler in reflection.get('separate_samplers', []):
                    pairs['SPIRV_Cross_Combined' + image['name'] + sampler['name']] = (image, sampler)
            bindings = []
            def bind_sampler(match):
                kind, name = match[1], match[2]
                if name not in pairs or len(bindings) >= 32:
                    raise ValueError('Unmapped combined sampler or native binding limit: ' + name)
                image, sampler = pairs[name]
                binding = len(bindings)
                bindings.append({'native_binding': binding, 'name': name,
                                 'image': image, 'sampler': sampler})
                return f'layout(binding = {binding}) uniform {kind} {name};'
            text = re.sub(r'^uniform (\w*sampler\w*) (SPIRV_Cross_Combined\w+);$',
                          bind_sampler, glsl.read_text(), flags=re.MULTILINE)
            glsl.write_text(text)
            run([args.uam, '-s', file.suffix[1:], '-o', str(native), str(glsl)])
            output = native.read_bytes()
            assert output[:4] == b'DKSH' and len(output) >= 24
            header = struct.unpack_from('<6I', output)
            assert header[1] == 24 and header[2] + header[3] == len(output) and header[5] == 1
            manifest = {'shader': file.name, 'native_module': native.name,
                        'constant_blocks': blocks, 'copy_plan_checked': True,
                        'native_sampler_bindings': bindings,
                        'resources': {key: reflection[key] for key in
                                      ('ssbos', 'textures', 'separate_images', 'separate_samplers')
                                      if key in reflection},
                        'resource_bindings_verified_on_gpu': False,
                        'floating_point_edge_cases_verified': False}
            Path(str(prefix) + '.abi.json').write_text(json.dumps(manifest, indent=2) + '\n')
            item.update(result='COMPILED', native_bytes=len(output),
                        native_sha256=hashlib.sha256(output).hexdigest(),
                        abi_repack_checked=True, blocks=len(blocks))
        except (ValueError, AssertionError, OSError) as error:
            item.update(result='UNSUPPORTED', diagnostic=str(error))
        return item

    with ThreadPoolExecutor(max_workers=4) as pool:
        programs = list(pool.map(build, inputs))
    print(json.dumps({'result': 'EXPERIMENTAL', 'variants': len(programs),
                      'compiled_modules': sum(p['result'] == 'COMPILED' for p in programs),
                      'programs': programs, 'constant_abi_changed': True,
                      'shader_instructions_patched_in_spirv': False,
                      'runtime_backend_integrated': False, 'hardware_rendering_verified': False}, indent=2))


if __name__ == '__main__':
    main()

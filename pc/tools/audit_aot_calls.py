"""Bounded direct-branch audit of explicit analysis roots; no code execution.

Reports call destinations/IAT names, never raw code. A caller-provided root is
not evidence of a real PE entry, function boundary, or a reachable runtime path.
"""
import argparse
import json
from pathlib import Path
import struct
import sys
from audit_install import pe_info
from audit_startup import audit


def inspect(exe, analysis, entries, limit=20000):
    if not 1 <= limit <= 100000 or not 1 <= len(entries) <= 64:
        raise ValueError('Use 1..64 roots and 1..100000 instructions per root')
    identity = audit(exe, analysis)
    pe = pe_info(exe)
    raw = exe.read_bytes()
    def offset(rva, size):
        for section in pe['sections']:
            delta = rva-section['rva']
            if 0 <= delta and delta+size <= section['raw_bytes']:
                return section['raw_offset']+delta
        raise ValueError('Import RVA outside file sections')
    def word(rva):
        return struct.unpack_from('<I', raw, offset(rva, 4))[0]
    def string(rva):
        result = bytearray()
        for i in range(1024):
            value = raw[offset(rva+i, 1)]
            if not value:
                return result.decode('ascii')
            result.append(value)
        raise ValueError('Unterminated import string')
    nt = struct.unpack_from('<I', raw, 60)[0]
    rva, size = struct.unpack_from('<II', raw, nt+24+104)
    imports = {}
    if rva:
        for i in range(min(size//20, 4096)):
            original, timestamp, chain, dll, iat = struct.unpack_from('<5I', raw, offset(rva+i*20, 20))
            if not any((original, timestamp, chain, dll, iat)):
                break
            module = string(dll)
            for j in range(4096):
                value = word((original or iat)+j*4)
                if not value:
                    break
                symbol = '#'+str(value & 65535) if value & 0x80000000 else string(value+2)
                imports[pe['image_base']+iat+j*4] = module+'!'+symbol
            else:
                raise ValueError('Unterminated import lookup')
        else:
            raise ValueError('Unterminated import table')
    image = (analysis/'fnv-analysis.xbe').read_bytes()
    count, table = struct.unpack_from('<II', image, 0x11c)
    section = next(s for s in pe['sections'] if s['name'] == '.text')
    start = pe['image_base']+section['rva']
    rows = [struct.unpack_from('<5I', image, table-pe['image_base']+i*56) for i in range(count)]
    row = next(r for r in rows if r[1] == start)
    code = image[row[3]:row[3]+section['virtual_bytes']]
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_GRP_JUMP
    from capstone.x86_const import X86_OP_IMM, X86_OP_MEM
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    roots = []
    for entry in entries:
        if not start <= entry < start+len(code):
            raise ValueError('Root outside analysis .text')
        pending, visited, calls, stops = [entry], set(), [], []
        while pending and len(visited) < limit:
            at = pending.pop()
            if at in visited:
                continue
            if not start <= at < start+len(code):
                stops.append(dict(site=at, reason='outside-text'))
                continue
            instruction = next(decoder.disasm(code[at-start:at-start+15], at, count=1), None)
            if instruction is None:
                stops.append(dict(site=at, reason='decode-failed'))
                continue
            visited.add(at)
            if instruction.mnemonic in ('ret', 'retf', 'int3', 'ud2', 'hlt'):
                stops.append(dict(site=at, reason=instruction.mnemonic))
                continue
            if instruction.mnemonic == 'call' or instruction.group(CS_GRP_JUMP) or instruction.mnemonic.startswith('loop'):
                operand = instruction.operands[0]
                target = operand.imm & 0xffffffff if operand.type == X86_OP_IMM else None
                item = dict(site=at, target=target)
                if operand.type == X86_OP_MEM and not operand.mem.base and not operand.mem.index and not operand.mem.segment:
                    item['import'] = imports.get(operand.mem.disp & 0xffffffff)
                if instruction.mnemonic == 'call':
                    calls.append(item)
                elif target is not None:
                    pending.append(target)
                else:
                    stops.append(dict(item, reason='indirect-branch'))
                if instruction.mnemonic == 'jmp':
                    continue
            pending.append(at+instruction.size)
        roots.append(dict(entry=entry, instructions=len(visited), limit_reached=bool(pending),
                          calls=sorted(calls, key=lambda c: c['site']), stops=stops))
    # Recheck all input identities after decoding.
    if audit(exe, analysis) != identity:
        raise ValueError('Audit inputs changed')
    return dict(status='ANALYSIS_ONLY', identity=identity, roots=roots,
                limits=['Explicit roots are hypotheses, not verified startup entries',
                        'Calls are recorded, not followed; indirect/exception edges unresolved',
                        'Instruction boundaries and code/data classification are not independently proven',
                        'No game execution, no provider compatibility claim'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'analysis', 'python-deps', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--entry', action='append', type=lambda s: int(s, 0), required=True)
    parser.add_argument('--limit', type=int, default=20000)
    a = parser.parse_args()
    sys.path.insert(0, str(a.python_deps.resolve()))
    result = inspect(a.exe, a.analysis, a.entry, a.limit)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('x') as f:
        json.dump(result, f, indent=2)
        f.write('\n')
    print('ANALYSIS_ONLY', [(hex(r['entry']), r['instructions'], len(r['calls'])) for r in result['roots']])

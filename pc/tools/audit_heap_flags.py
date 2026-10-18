"""Read-only static evidence for immediate flags at direct heap IAT calls.

Requires a hash-verified analysis container and the preserved function index.
Only consecutive PUSH arguments are interpreted. Unknown values, wrappers,
dynamic imports and execution paths remain unknown. Never patches an EXE.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys
from audit_install import pe_info
from audit_aot_gaps import Intervals, address, sections_from_container
from aot_code_data import read_va


def sha(data):
    return hashlib.sha256(data).hexdigest()


def audit(exe, analysis, decoder):
    profile = json.loads((analysis/'input-profile.json').read_text())
    raw = (analysis/'fnv-analysis.xbe').read_bytes()
    functions_data = (analysis/'disasm/functions.json').read_bytes()
    identity = json.loads((analysis/'scan-identity.json').read_text())
    original = exe.read_bytes()
    if (sha(raw) != profile['analysis_container_sha256'] or
        sha(original) != profile['executable_sha256'] or
        sha(raw) != identity['input_sha256'] or
        sha(functions_data) != identity['files']['functions.json']):
        raise ValueError('Analysis/executable/function index identity mismatch')
    pe = pe_info(exe)
    def offset(rva, size):
        for s in pe['sections']:
            d = rva-s['rva']
            if 0 <= d and d+size <= s['raw_bytes']:
                return s['raw_offset']+d
        raise ValueError('Import descriptor outside file backing')
    nt = struct.unpack_from('<I', original, 60)[0]
    imports_rva = struct.unpack_from('<I', original, nt+24+104)[0]
    calls = {'HeapCreate': 3, 'HeapAlloc': 3, 'HeapReAlloc': 4, 'HeapFree': 3,
             'HeapSize': 3, 'HeapValidate': 3}
    slots = {}
    for i, dll in enumerate(pe['imports']):
        first_thunk = struct.unpack_from('<I', original, offset(imports_rva+i*20, 20)+16)[0]
        if dll['dll'].lower() != 'kernel32.dll':
            continue
        for index, symbol in enumerate(dll['symbols']):
            if symbol.get('name') in calls:
                slots[pe['image_base']+first_thunk+index*4] = symbol['name']
    sections = sections_from_container(raw, profile)
    intervals = Intervals(json.loads(functions_data))
    from capstone.x86 import X86_OP_IMM
    findings, totals = [], {}
    for slot, name in slots.items():
        evidence, bounded, unresolved = Counter(), Counter(), 0
        for section in sections:
            if not section['executable']:
                continue
            data = raw[section['offset']:section['offset']+section['stored']]
            pattern = b'\xff\x15'+struct.pack('<I', slot)
            cursor = data.find(pattern)
            while cursor >= 0:
                va = section['start']+cursor
                owners = intervals.owners(va)
                boundary, flags, possible = False, None, None
                for start, end in sorted(owners, key=lambda item: item[1]-item[0]):
                    code = read_va(raw, sections, start, end-start)
                    if code is None:
                        continue
                    previous = []
                    for ins in decoder.disasm(code, start):
                        if ins.address > va:
                            break
                        if ins.address == va and ins.mnemonic == 'call' and ins.bytes == pattern:
                            boundary = True
                            n = calls[name]
                            args = previous[-n:]
                            if len(args) == n and all(p.mnemonic == 'push' for p in args):
                                # Stack order is right-to-left. Create's flags are
                                # argument zero, all other heap calls use arg one.
                                operand = args[-1 if name == 'HeapCreate' else -2].operands[0]
                                if operand.type == X86_OP_IMM:
                                    flags = operand.imm & 0xffffffff
                            # Exact CRT idiom: xor eax,eax; cmp ...,eax;
                            # push maximum; sete al; push initial; push eax.
                            # No instruction here can set EAX outside {0,1}.
                            tail = previous[-6:]
                            if (name == 'HeapCreate' and len(tail) == 6 and
                                [p.mnemonic for p in tail] == ['xor','cmp','push','sete','push','push'] and
                                tail[0].op_str == 'eax, eax' and tail[3].op_str == 'al' and
                                tail[5].op_str == 'eax' and all(tail[i].address+tail[i].size == tail[i+1].address for i in range(5))):
                                possible = [0,1]
                            break
                        previous.append(ins)
                    if boundary:
                        break
                if boundary:
                    if flags is None:
                        unresolved += 1
                        if possible:
                            bounded[tuple(possible)] += 1
                    else:
                        evidence[flags] += 1
                    findings.append(dict(api=name, address=va, immediate_flags=flags, possible_flags=possible))
                cursor = data.find(pattern, cursor+1)
        totals[name] = dict(immediate_flags={str(k): v for k, v in sorted(evidence.items())},
                            bounded_flags={','.join(map(str,k)):v for k,v in sorted(bounded.items())},unknown_flags=unresolved)
    return dict(executable_sha256=sha(original), analysis_sha256=sha(raw),
                static_candidates_only=True, runtime_flags_verified=False,
                totals=totals, calls=findings,
                limits=['Only direct IAT calls on decoded function boundaries',
                        'Reachability and runtime arguments are not established',
                        'Register arguments, wrappers and dynamic imports remain unknown'])


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'analysis', 'python-deps', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    sys.path.insert(0, str(a.python_deps.resolve()))
    import capstone
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    decoder.detail = True
    report = audit(a.exe, a.analysis, decoder)
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report['totals'], indent=2))

"""Read-only evidence for unresolved x86 AOT targets; never registers entries.

The detailed output contains game bytes and must stay private. A decodable
address, alignment or prologue is not proof of a callable function.
"""
import argparse
from bisect import bisect_right
from collections import Counter, defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct
import sys


def sha(data):
    return hashlib.sha256(data).hexdigest()


def address(value):
    return int(value, 0) if isinstance(value, str) else int(value)


class Intervals:
    """All containing intervals, including nested/overlapping function ranges."""
    def __init__(self, functions):
        self.items = sorted((address(f['start']), address(f['end'])) for f in functions)
        self.starts = [start for start, _ in self.items]
        self.maximum = []
        end = 0
        for start, stop in self.items:
            if not 0 <= start < stop <= 0x100000000:
                raise ValueError('Invalid detected function range')
            end = max(end, stop)
            self.maximum.append(end)

    def owners(self, target):
        i = bisect_right(self.starts, target)-1
        result = []
        while i >= 0 and self.maximum[i] > target:
            start, end = self.items[i]
            if start <= target < end:
                result.append((start, end))
            i -= 1
        return sorted(set(result))


def sections_from_container(raw, profile):
    if len(raw) < 0x178 or raw[:4] != b'XBEH':
        raise ValueError('Invalid analysis container header')
    base = struct.unpack_from('<I', raw, 0x104)[0]
    count, table = struct.unpack_from('<II', raw, 0x11c)
    table -= base
    if base != profile['image_base'] or count != len(profile['sections']) or \
            not 0 < count <= 32 or table < 0 or table+count*56 > len(raw):
        raise ValueError('Analysis section table mismatch')
    result = []
    for i, source in enumerate(profile['sections']):
        flags, start, size, offset, stored = struct.unpack_from('<5I', raw, table+i*56)
        # Older accepted profiles predate the explicit analysis_raw_bytes field.
        # Their sole replacement was .text with exactly virtual_bytes captured.
        expected_stored = source.get('analysis_raw_bytes',
            source['virtual_bytes'] if source['name'] == '.text' and
            profile.get('code_source') == 'captured-loaded-text' else source['raw_bytes'])
        if start != base+source['rva'] or size < source['virtual_bytes'] or \
                size != max(source['virtual_bytes'], expected_stored) or \
                stored != expected_stored or offset+stored > len(raw) or \
                stored > size or start+size > base+profile['image_bytes'] or \
                bool(flags & 4) != bool(source['flags'] & 0x20000000):
            raise ValueError('Analysis section mapping mismatch')
        item = dict(name=source['name'], start=start, end=start+size,
                    offset=offset, stored=stored, executable=bool(flags & 4))
        if any(start < p['end'] and p['start'] < start+size for p in result):
            raise ValueError('Overlapping analysis sections')
        result.append(item)
    return result


def classify(target, profile, sections, ranges):
    owners = ranges.owners(target)
    section = next((s for s in sections if s['start'] <= target < s['end']), None)
    if section is None:
        kind = 'image-hole' if profile['image_base'] <= target < \
            profile['image_base']+profile['image_bytes'] else 'outside-image'
    elif not section['executable']:
        kind = 'non-executable-section'
    elif any(start == target for start, _ in owners):
        kind = 'detected-entry'
    elif owners:
        kind = 'inside-detected-body'
    else:
        kind = 'executable-gap'
    return kind, section, owners


def context(raw, section, target, ranges, decoder):
    """Bounded inspection only; no boundary claims from decoding at target."""
    relative = target-section['start']
    if relative >= section['stored']:
        return dict(storage='zero-fill', bytes_hex='', decode=[], owner_boundaries=[])
    offset = section['offset']+relative
    available = section['stored']-relative
    code = raw[offset:offset+min(64, available)]
    decoded = [dict(address=f'0x{i.address:08X}', size=i.size,
                    mnemonic=i.mnemonic, operands=i.op_str)
               for i in decoder.disasm(code, target, count=8)]
    evidence = []
    for start, _ in ranges:
        distance = target-start
        status = 'not-checked-distance-limit'
        if distance <= 4096:
            owner_offset = section['offset']+start-section['start']
            # An overlap may start outside this section; do not read another range.
            if start < section['start']:
                status = 'owner-outside-section'
            else:
                owner_bytes = raw[owner_offset:owner_offset+min(distance+15,
                    section['stored']-(start-section['start']))]
                status = 'decode-stopped-before-target'
                for insn in decoder.disasm(owner_bytes, start):
                    if insn.address == target:
                        status = 'on-owner-linear-boundary'
                        break
                    if insn.address < target < insn.address+insn.size:
                        status = 'inside-owner-linear-instruction'
                        break
        evidence.append(dict(owner=f'0x{start:08X}', status=status))
    return dict(storage='file-backed', bytes_hex=code[:32].hex(), decode=decoded,
                owner_boundaries=evidence)


def boundary_group(record):
    statuses = {e['status'] for e in record.get('owner_boundaries', [])}
    on = 'on-owner-linear-boundary' in statuses
    inside = 'inside-owner-linear-instruction' in statuses
    if on and inside:
        return 'mixed-owner-decodes'
    if on:
        return 'boundary-seen'
    if inside:
        return 'inside-instruction-only'
    if not statuses:
        return 'no-owner-evidence'
    return 'decode-inconclusive'


def strip_c_noncode(text):
    """Remove generated C comments/literals while preserving line numbers."""
    token = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    # C replaces comments with whitespace; deleting them can join two tokens
    # and invent a symbol that never existed in the compiled source.
    return re.sub(token, lambda match: ' '+'\n'*match[0].count('\n')+' ', text, flags=re.S)


def source_references(text, targets):
    """Locate actual symbol uses in the SDK's split-C output dialect."""
    function = None
    block = None
    records = []
    for number, line in enumerate(strip_c_noncode(text).splitlines(), 1):
        definition = re.match(r'\s*void\s+(\w+)\(void\)\s*$', line)
        if definition:
            function = definition[1]
            block = None
            continue
        label = re.match(r'loc_([0-9A-Fa-f]{8}):', line)
        if label:
            block = int(label[1], 16)
        for match in re.finditer(r'\bsub_([0-9A-Fa-f]{8})\b', line):
            target = int(match[1], 16)
            if target not in targets:
                continue
            if 'RECOMP_ABI_CALL(' in line:
                kind = 'abi-call'
            elif re.search(r'\bif\s*\(', line):
                kind = 'conditional-transfer'
            elif re.search(r'\breturn\s*;', line):
                kind = 'tail-transfer'
            else:
                kind = 'other-symbol-use'
            records.append(dict(target=target, function=function,
                                block=f'0x{block:08X}' if block is not None else None,
                                line=number, kind=kind))
    return records


def trace_generated(analysis, report, targets):
    references = defaultdict(list)
    names = sorted(name for name in report['output_files'] if re.fullmatch(r'recomp_\d{4}\.c', name))
    if not names:
        raise ValueError('No recorded generated C chunks for provenance')
    for name in names:
        data = (analysis/'generated'/name).read_bytes()
        if sha(data) != report['output_files'][name]:
            raise ValueError('Modified generated source: '+name)
        for ref in source_references(data.decode('utf-8'), targets):
            target = ref.pop('target')
            references[target].append(dict(file=name, **ref))
    return references, len(names)


def audit(analysis, output, deps=None, with_generated=False):
    if deps:
        sys.path.insert(0, str(deps.resolve()))
    import capstone
    analysis = analysis.resolve()
    def read(name):
        return (analysis/name).read_bytes()
    profile_bytes = read('input-profile.json')
    profile = json.loads(profile_bytes)
    raw = read('fnv-analysis.xbe')
    report = json.loads(read('generation-report.json'))
    scan_bytes = read('scan-identity.json')
    scan = json.loads(scan_bytes)
    if profile.get('analysis_only') is not True or \
            sha(raw) != profile['analysis_container_sha256'] or \
            sha(raw) != scan['input_sha256'] or \
            sha(profile_bytes) != report['input_profile_sha256'] or \
            scan['sdk_sha256'] != report['sdk_sha256'] or \
            scan['index_wrapper_sha256'] != report['index_wrapper_sha256']:
        raise ValueError('Input/generation/scan identity mismatch')
    names = ('functions.json', 'xrefs.json')
    files = {name: read('disasm/'+name) for name in names}
    for name, data in files.items():
        if sha(data) != scan['files'][name]:
            raise ValueError('Modified scan file: '+name)
    stub_name = 'recomp_stubs_unresolved.c'
    stubs = read('generated/'+stub_name)
    if sha(stubs) != report['output_files'][stub_name]:
        raise ValueError('Modified unresolved stubs')
    matches = re.findall(rb'recomp_unimpl\("untranslated call target",\s*0x([0-9a-fA-F]+)u\)', stubs)
    targets = {int(v, 16) for v in matches}
    if len(matches) != len(targets) or len(targets) != report['gaps']['unresolved_call_targets']:
        raise ValueError('Unresolved target count mismatch')
    provenance, chunk_count = trace_generated(analysis, report, targets) if with_generated else ({}, 0)
    functions = json.loads(files['functions.json'])
    ranges = Intervals(functions)
    sections = sections_from_container(raw, profile)
    refs = defaultdict(list)
    for ref in json.loads(files['xrefs.json']):
        to = address(ref['to'])
        if to in targets:
            refs[to].append((address(ref['from']), str(ref['type'])))
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    records = []
    for target in sorted(targets):
        kind, section, owners = classify(target, profile, sections, ranges)
        callers = [dict(address=f'0x{source:08X}', type=kind,
                        owners=[f'0x{start:08X}' for start, _ in ranges.owners(source)])
                   for source, kind in sorted(set(refs[target]))]
        item = dict(address=f'0x{target:08X}', classification=kind,
                    section=section['name'] if section else None,
                    owners=[dict(start=f'0x{start:08X}', end=f'0x{end:08X}') for start, end in owners],
                    xrefs=callers, xref_counts=dict(Counter(r['type'] for r in callers)))
        if with_generated:
            item['generated_references'] = provenance.get(target, [])
        if section:
            item.update(context(raw, section, target, owners, decoder))
        records.append(item)
    result = dict(status='AUDIT_ONLY_NO_ENTRIES_REGISTERED',
        audit_tool_sha256=sha(Path(__file__).read_bytes()),
        input_profile_sha256=sha(profile_bytes), scan_identity_sha256=sha(scan_bytes),
        unresolved_stub_sha256=sha(stubs), decoder_version=capstone.__version__,
        unresolved_targets=len(targets),
        classification_counts=dict(Counter(r['classification'] for r in records)),
        boundary_evidence_target_counts=dict(Counter(boundary_group(r) for r in records)),
        owner_boundary_counts=dict(Counter(e['status'] for r in records for e in r.get('owner_boundaries', []))),
        targets_without_scan_xrefs=sum(not r['xrefs'] for r in records), targets=records,
        native_engine_started=False, cold_boot_supported=False,
        limits=['Linear decoding is evidence only, not control-flow/ABI verification',
                'Detected function intervals are heuristic and may overlap',
                'No entry is accepted from alignment, prologue or successful decode alone',
                'Audit report includes game bytes; keep private'])
    if with_generated:
        result['generated_provenance'] = dict(chunks_verified=chunk_count,
            total_symbol_uses=sum(len(v) for v in provenance.values()),
            targets_without_symbol_use=sum(not provenance.get(t) for t in targets),
            missing_scan_xref_use_kinds=dict(Counter(ref['kind'] for record in records
                if not record['xrefs'] for ref in record['generated_references'])),
            limits='C block labels are not exact guest instruction addresses; conditional-transfer includes guarded paths')
    # Do not replace any accepted analysis input or generated source with a report.
    output = output.resolve()
    if output.exists():
        raise ValueError('Audit output already exists; choose a new private report')
    with output.open('x', encoding='utf-8') as handle:
        handle.write(json.dumps(result, indent=2)+'\n')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--analysis', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--python-deps', type=Path)
    parser.add_argument('--trace-generated', action='store_true', help='Hash and trace all recorded C chunks (private output)')
    args = parser.parse_args()
    result = audit(args.analysis, args.output, args.python_deps, args.trace_generated)
    print(json.dumps({key: value for key, value in result.items() if key != 'targets'}, indent=2))

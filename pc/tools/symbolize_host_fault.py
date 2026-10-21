"""Locate a diagnostic host exception in the exact linked PE's text symbols.

The last guest indirect-call site is historical context, not the faulting PC.
Inputs and reports can contain private addresses; keep reports under local/.
"""
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pe_layout(raw):
    def unpack(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(raw):
            raise ValueError('Truncated PE')
        return struct.unpack_from(fmt, raw, offset)
    if raw[:2] != b'MZ':
        raise ValueError('Missing MZ')
    nt, = unpack('<I', 60)
    if raw[nt:nt+4] != b'PE\0\0':
        raise ValueError('Missing PE signature')
    sections, = unpack('<H', nt+6)
    optional_size, = unpack('<H', nt+20)
    optional = nt+24
    magic, = unpack('<H', optional)
    if magic not in (0x10b, 0x20b) or optional_size < 64 or not 0 < sections <= 96:
        raise ValueError('Unsupported PE layout')
    unpack(f'<{optional_size}s', optional)
    base, = unpack('<I' if magic == 0x10b else '<Q', optional+(28 if magic == 0x10b else 24))
    size, = unpack('<I', optional+56)
    executable = []
    for i in range(sections):
        at = optional+optional_size+i*40
        unpack('<40s', at)
        length, rva = unpack('<II', at+8)
        flags, = unpack('<I', at+36)
        if rva+length > size:
            raise ValueError('Section outside image')
        if flags & 0x20000000:
            executable.append((rva, rva+length))
    return base, size, executable


FAULT = re.compile(r'^FNV_HOST_EXCEPTION code=0x([0-9a-fA-F]+) pc=0x([0-9a-fA-F]+) '
                   r'image_offset=0x([0-9a-fA-F]+) '
                   r'(?:guest_site|last_indirect_site)=0x([0-9a-fA-F]+) thread=(\d+)', re.M)


def locate(raw, log, symbols):
    base, size, executable = pe_layout(raw)
    # nm may emit data/debug symbols between two functions. They do not own PC.
    code = {}
    for line in symbols.splitlines():
        match = re.fullmatch(r'\s*([0-9a-fA-F]+)\s+([Tt])\s+(.+?)\s*', line)
        if match:
            address = int(match[1], 16)
            if base <= address < base+size:
                code.setdefault(address, set()).add(match[3])
    addresses = sorted(code)
    result = []
    for match in FAULT.finditer(log):
        status, pc, offset, guest, thread = (int(v, 16 if i < 4 else 10) for i, v in enumerate(match.groups()))
        if pc < offset or not any(lo <= offset < hi for lo, hi in executable):
            raise ValueError('Fault PC offset is outside executable image sections')
        linked = base+offset
        index = bisect.bisect_right(addresses, linked)-1
        preceding = None
        if index >= 0:
            start = addresses[index]
            # A preceding symbol in another code section is not a match.
            if any(lo <= start-base <= offset < hi for lo, hi in executable):
                preceding = dict(address=start, names=sorted(code[start]), offset=linked-start)
        result.append(dict(exception_code=status, runtime_pc=pc, runtime_image_base=pc-offset,
                           image_offset=offset, linked_pc=linked, thread=thread,
                           last_indirect_site=guest, preceding_text_symbol=preceding))
    if not result:
        raise ValueError('No host exception records found')
    return dict(status='SYMBOLIZED', faults=result,
                limitation='Nearest preceding text symbols are candidates, not debug line or function-extent proof. '
                           'last_indirect_site is not the faulting guest instruction.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('exe', 'log', 'nm', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    a = parser.parse_args()
    inputs = {str(p.resolve()): sha(p) for p in (a.exe, a.log, a.nm)}
    run = subprocess.run([str(a.nm.resolve()), '--numeric-sort', str(a.exe.resolve())],
                         capture_output=True, text=True, timeout=120)
    if run.returncode:
        raise RuntimeError(run.stderr)
    report = locate(a.exe.read_bytes(), a.log.read_text(), run.stdout)
    if inputs != {str(p.resolve()): sha(p) for p in (a.exe, a.log, a.nm)}:
        raise ValueError('Inputs changed while symbolizing')
    report['input_sha256'] = inputs
    a.output.parent.mkdir(parents=True, exist_ok=True)
    with a.output.open('x', newline='\n') as f:
        json.dump(report, f, indent=2)
        f.write('\n')
    print(report['status'], len(report['faults']), 'host fault(s)')


if __name__ == '__main__':
    main()

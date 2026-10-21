"""Repair stale overlapping instruction records after table-target resynchronization.

Independent SDK overlay: no game addresses or hand-edited generated bodies.
Conflicting authoritative instruction starts fail closed.
"""

def install():
    from tools.recomp.disasm import Disassembler
    if getattr(Disassembler.disassemble_function, '_fnv_nonoverlap_resync', False):
        return
    original = Disassembler.disassemble_function

    def decode(self, raw_bytes, start_va, end_va, resync=None):
        points = sorted(set(resync or ()))
        if not points:
            return original(self, raw_bytes, start_va, end_va, resync)
        size = end_va - start_va
        if size <= 0 or size > len(raw_bytes):
            return []
        points = [p for p in points if start_va <= p < end_va]
        decoded = {i.address: self._decode_instruction(i)
                   for i in self._cs.disasm(raw_bytes[:size], start_va)}
        for point in points:
            for address in list(decoded):
                if address < point < decoded[address].end_address:
                    del decoded[address]
            for item in self._cs.disasm(raw_bytes[point-start_va:size], point):
                if item.address != point and item.address in decoded:
                    break
                end = item.address + item.size
                if any(item.address < target < end for target in points):
                    if item.address == point:
                        raise ValueError('Conflicting authoritative instruction starts')
                    break  # Discard speculative table bytes before next case.
                # A new instruction can cover several starts from the old
                # misaligned linear stream, not just the instruction at point.
                for address in list(decoded):
                    old = decoded[address]
                    if address < end and old.end_address > item.address:
                        del decoded[address]
                decoded[item.address] = self._decode_instruction(item)
        result = [decoded[a] for a in sorted(decoded)]
        if any(a.end_address > b.address for a, b in zip(result, result[1:])):
            raise ValueError('Overlapping instruction records after resynchronization')
        return result

    decode._fnv_nonoverlap_resync = True
    Disassembler.disassemble_function = decode


if __name__ == '__main__':
    import argparse
    import runpy
    import sys
    from pathlib import Path
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--python-deps', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    sys.path[:0] = [str(args.sdk.resolve()), str(args.python_deps.resolve())]
    install()
    # Keep frame/SIMD/multiply options in the existing wrapper. This explicit
    # opt-in does not alter the identities of previously frozen generations.
    sys.argv = ['aot_frame_restore', '--sdk', str(args.sdk.resolve()), *remaining]
    runpy.run_path(str(Path(__file__).with_name('aot_frame_restore.py')), run_name='__main__')

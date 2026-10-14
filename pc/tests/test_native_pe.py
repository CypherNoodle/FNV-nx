"""Check data-only PE mapping/IAT binding against independent file parsing.
Test tokens are identifiers, not Windows implementations. Never execute x86.
"""
import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from pe_fixture import write_pe

pc = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(pc / "tools"))
from audit_install import pe_info

def oracle(path):
    raw = path.read_bytes()
    pe = pe_info(path)
    nt = struct.unpack_from("<I", raw, 60)[0]
    headers = struct.unpack_from("<I", raw, nt + 24 + 60)[0]
    mapped = bytearray(pe["image_bytes"])
    mapped[:headers] = raw[:headers]
    for s in pe["sections"]:
        mapped[s["rva"]:s["rva"]+s["raw_bytes"]] = raw[s["raw_offset"]:s["raw_offset"]+s["raw_bytes"]]
    imports = []
    directory, size = struct.unpack_from("<II", raw, nt + 24 + 104)
    if directory:
        for at in range(directory, directory+size, 20):
            fields = struct.unpack_from("<5I", mapped, at)
            if not any(fields):
                break
            lookup, _, _, dll, iat = fields
            dll_name = bytes(mapped[dll:mapped.index(0, dll)]).decode("ascii").lower()
            for j in range(4096):
                value = struct.unpack_from("<I", mapped, (lookup or iat)+4*j)[0]
                if not value:
                    break
                ordinal = value & 65535 if value & 0x80000000 else 0
                name = "" if value & 0x80000000 else bytes(mapped[value+2:mapped.index(0, value+2)]).decode("ascii")
                imports.append((iat+4*j, ordinal, int(bool(value & 0x80000000)), dll_name, name))
    return pe, mapped, imports

def test(cc, game):
    with tempfile.TemporaryDirectory(prefix="fnv-native-pe-") as tmp:
        root = Path(tmp)
        host = root / "native-pe"
        subprocess.run([*cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I"+str(pc / "native"),
            str(pc / "native/pe_image.c"), str(pc / "tests/native_pe_host.c"), "-o", str(host)], check=True)
        def run(path, mode="collect", capacity=32*1024*1024):
            out = root / "mapped.bin"
            out.unlink(missing_ok=True)
            result = subprocess.run([str(host), str(path), str(out), mode, str(capacity)],
                                    text=True, capture_output=True, check=True)
            lines = result.stdout.splitlines()
            head = list(map(int, lines[0].split()))
            records = []
            for line in lines[1:]:
                row = line.split("\t")
                records.append(tuple(map(int, row[:3]))+tuple(row[3:]))
            return head, records, out.read_bytes() if out.exists() else None
        def check(path):
            pe, mapped, records = oracle(path)
            head, actual, data = run(path)
            assert head == [0, pe["image_base"], len(mapped), pe["entry_va"]-pe["image_base"], len(records), 0, 1]
            assert data == mapped and actual == records
            head, actual, data = run(path, "bind")
            for index, (iat, *_) in enumerate(records):
                struct.pack_into("<I", mapped, iat, 0xf1000000+4*(index+1))
            assert head[0] == 0 and head[5] == len(records) and data == mapped and actual == records
            if records:
                _, original, _ = oracle(path)
                for mode, status in (("missing", 4), ("zero", 4), ("short", 3)):
                    head, _, data = run(path, mode)
                    assert head[0] == status and head[6] == 1 and data == original, mode
            assert run(path, capacity=4096)[0][0] == 3
            return len(records)
        path = root / "fixture.exe"
        write_pe(path, ("KERNEL32.dll", "USER32.dll"))
        check(path)  # Ordinal imports, shared lookup/IAT.
        raw = bytearray(path.read_bytes())
        nt = struct.unpack_from("<I", raw, 60)[0]
        optional = nt+24
        def rva_offset(rva): return rva-0x1000+0x200
        directory = struct.unpack_from("<I", raw, optional+104)[0]
        lookup = struct.unpack_from("<I", raw, rva_offset(directory))[0]
        symbol = b"\0\0CreateFileA\0"
        raw[0xa00:0xa00+len(symbol)] = symbol
        struct.pack_into("<I", raw, rva_offset(lookup), 0x1800)
        # Separate OriginalFirstThunk / FirstThunk.
        raw[0xc00:0xc08] = raw[rva_offset(lookup):rva_offset(lookup)+8]
        struct.pack_into("<I", raw, rva_offset(directory)+16, 0x1a00)
        # BSS tail plus unmapped hole between headers and first section.
        struct.pack_into("<I", raw, optional+56, 0x3000)
        struct.pack_into("<I", raw, optional+224+8, 0x2000)
        path.write_bytes(raw)
        check(path)
        for mutate in ("machine", "raw", "virtual", "headers", "base", "entry", "truncated", "overlap"):
            invalid = bytearray(raw)
            if mutate == "machine": struct.pack_into("<H", invalid, nt+4, 0x8664)
            elif mutate == "raw": struct.pack_into("<I", invalid, optional+224+16, 0x3000)
            elif mutate == "virtual": struct.pack_into("<I", invalid, optional+224+8, 0xffffffff)
            elif mutate == "headers": struct.pack_into("<I", invalid, optional+60, 0x80)
            elif mutate == "base": struct.pack_into("<I", invalid, optional+28, 0xfffff000)
            elif mutate == "entry": struct.pack_into("<I", invalid, optional+16, 0x800)
            elif mutate == "truncated": invalid = invalid[:400]
            else:
                struct.pack_into("<H", invalid, nt+6, 2)
                invalid[optional+224+40:optional+224+80] = invalid[optional+224:optional+224+40]
            path.write_bytes(invalid)
            assert run(path)[0][0] == 2, mutate
        for mutate in ("unsafe_dll", "lookup", "name", "iat", "duplicate", "ordinal", "descriptor"):
            invalid = bytearray(raw)
            if mutate == "unsafe_dll":
                dll = struct.unpack_from("<I", invalid, rva_offset(directory)+12)[0]
                invalid[rva_offset(dll):rva_offset(dll)+10] = b"../foo.dll\0"
            elif mutate == "lookup": struct.pack_into("<I", invalid, rva_offset(directory), 0xfffffffc)
            elif mutate == "name": struct.pack_into("<I", invalid, rva_offset(lookup), 0x800)
            elif mutate == "iat": struct.pack_into("<I", invalid, rva_offset(directory)+16, 0x2ffc)
            elif mutate == "duplicate": struct.pack_into("<I", invalid, rva_offset(directory)+20+16, 0x1a00)
            elif mutate == "ordinal": struct.pack_into("<I", invalid, rva_offset(lookup), 0x80010001)
            else: struct.pack_into("<I", invalid, optional+108, 40)
            path.write_bytes(invalid)
            head, _, data = run(path, "bind")
            assert head[0] == 2 and head[5] == 0 and head[6] == 1, mutate
        write_pe(path)
        check(path)
        actual_count = check(game / "FalloutNV.exe") if game else None
        print("PASS: PE sections/BSS, named/ordinal imports, atomic IAT, bounds, malformed tables; real EXE imports:", actual_count,
              "; no guest code executed")

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cc", nargs="+", default=["cc"])
    p.add_argument("--game", type=Path)
    args = p.parse_args()
    test(args.cc, args.game)

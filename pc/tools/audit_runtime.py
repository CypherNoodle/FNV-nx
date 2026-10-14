"""Check the game's static imports against the staged i386 DLL exports.

This does not execute DLLs or prove that an export implements the required
behavior. Dynamically loaded libraries and Steam authentication remain untested.
"""
import argparse, json, struct
from pathlib import Path
from audit_install import pe_info

def exports(path):
    b = path.read_bytes()
    def read(at, n):
        if at < 0 or n < 0 or at + n > len(b): raise ValueError("PE export range")
        return b[at:at+n]
    def u16(at): return struct.unpack("<H",read(at,2))[0]
    def u32(at): return struct.unpack("<I",read(at,4))[0]
    if read(0,2) != b"MZ": raise ValueError("Missing MZ")
    nt = u32(60)
    if read(nt,4) != b"PE\0\0" or u16(nt+4) != 0x14c: raise ValueError("Expected i386 provider")
    optional, size, count = nt+24, u16(nt+20), u16(nt+6)
    if size < 104 or u16(optional) != 0x10b or count > 96: raise ValueError("Export PE header")
    headers = u32(optional+60); sections = []
    for i in range(count):
        at = optional+size+i*40
        vsize, rva, raw_size, offset = struct.unpack_from("<4I",read(at,40),8)
        sections.append((rva,raw_size,offset))
    def offset(rva,n=1):
        if 0 <= rva and rva+n <= headers: read(rva,n); return rva
        for start, raw_size, at in sections:
            if 0 <= rva-start and rva-start+n <= raw_size: read(at+rva-start,n); return at+rva-start
        raise ValueError("Export RVA not backed")
    def string(rva):
        result=bytearray()
        for i in range(1024):
            byte=read(offset(rva+i),1)[0]
            if not byte: return result.decode("ascii")
            result.append(byte)
        raise ValueError("Export name bound")
    directory = u32(optional+96)
    if not directory: return set(),set()
    row = read(offset(directory,40),40)
    base, nfunc, nname, funcs, names, ords = struct.unpack_from("<6I",row,16)
    if nfunc > 200000 or nname > nfunc: raise ValueError("Export table bound")
    ordinals = {base+i for i in range(nfunc) if u32(offset(funcs+i*4,4))}
    found = set()
    for i in range(nname):
        ordinal = u16(offset(ords+i*2,2))
        if ordinal >= nfunc: raise ValueError("Export ordinal outside table")
        if base+ordinal in ordinals: found.add(string(u32(offset(names+i*4,4))))
    return found,ordinals

def audit(game, wine):
    game_dlls = {p.name.lower():p for p in game.iterdir() if p.is_file() and p.suffix.lower()==".dll"}
    wine_dlls = {p.name.lower():p for p in (wine / "drive_c/windows/syswow64").glob("*.dll")}
    dxvk = {p.name.lower():p for p in (wine / "drive_c/dxvk").glob("*.dll")}
    providers = {**wine_dlls, **dxvk, **game_dlls}
    result = []; missing = []
    # Include the game-owned media/Steam DLLs as import consumers as well.
    consumers = [game / "FalloutNV.exe"]; seen={consumers[0].resolve()}
    for consumer in consumers:
        for dependency in pe_info(consumer)["imports"]:
            name=dependency["dll"].lower(); provider=providers.get(name)
            if not provider:
                missing.append({"consumer":consumer.name,"dll":name,"reason":"missing DLL"}); continue
            if name in game_dlls and provider.resolve() not in seen:
                seen.add(provider.resolve()); consumers.append(provider)
                if len(consumers)>128: raise ValueError("Game DLL dependency graph exceeds bound")
            names,ordinals=exports(provider)
            absent=[s for s in dependency["symbols"] if (s["name"] not in names if "name" in s else s["ordinal"] not in ordinals)]
            result.append({"consumer":consumer.name,"dll":name,"symbols":len(dependency["symbols"]),"missing_symbols":absent})
            missing.extend({"consumer":consumer.name,"dll":name,**symbol} for symbol in absent)
    return {"static_import_coverage":"PASS" if not missing else "FAIL","modules_checked":len(consumers),
        "imports":result,"missing":missing,"engine_started":False,"dynamic_imports_checked":False,"export_behavior_verified":False}

if __name__ == "__main__":
    p=argparse.ArgumentParser(description=__doc__); p.add_argument("--game",required=True,type=Path)
    p.add_argument("--wine",required=True,type=Path); p.add_argument("--output",required=True,type=Path)
    a=p.parse_args(); report=audit(a.game,a.wine); a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(report,indent=2),encoding="utf-8")
    print(report["static_import_coverage"],"static imports;",len(report["missing"]),"missing; engine not executed")
    raise SystemExit(bool(report["missing"]))

"""Create a private x86 analysis container for the SH2/SH3 xboxrecomp tools.

The container is a build input, never an Xbox 360 executable or a runtime.
Original EXE is read-only. Captured text requires its exact declared hash and
an explicit analysis entry; this does not establish the real startup entry.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
from audit_install import pe_info, STEAM_SHA

def digest(data): return hashlib.sha256(data).hexdigest()
def align(n, boundary): return (n+boundary-1)&-boundary

def prepare(exe, output, expected_exe_sha, text_image=None, expected_text_sha=None, analysis_entry=None):
    raw = exe.read_bytes()
    if digest(raw) != expected_exe_sha:
        raise ValueError("Executable identity mismatch")
    pe = pe_info(exe)
    nt = struct.unpack_from("<I", raw, 60)[0]
    optional = nt+24
    optional_size = struct.unpack_from("<H", raw, nt+20)[0]
    table = optional+optional_size
    if optional_size<224 or struct.unpack_from("<I",raw,optional+92)[0]<16 or not 0<len(pe["sections"])<=32:
        raise ValueError("Analysis container requires full PE32 directories and at most 32 sections")
    if pe["image_base"]+pe["image_bytes"]>0x100000000:
        raise ValueError("Guest address wrap")
    sections = []
    for i, item in enumerate(pe["sections"]):
        if item["rva"]<4096 or item["rva"]+max(item["virtual_bytes"],item["raw_bytes"])>pe["image_bytes"]:
            raise ValueError("Invalid section mapping")
        flags = struct.unpack_from("<I", raw, table+i*40+36)[0]
        data = raw[item["raw_offset"]:item["raw_offset"]+item["raw_bytes"]]
        for previous in sections:
            if item["rva"]<previous["rva"]+max(previous["virtual_bytes"],previous["raw_bytes"]) and \
                previous["rva"]<item["rva"]+max(item["virtual_bytes"],item["raw_bytes"]):
                raise ValueError("Overlapping sections")
        sections.append(dict(item, flags=flags, data=data))
    text = next((s for s in sections if s["name"]==".text"),None)
    if text is None: raise ValueError("Missing .text code section")
    code_source = "file-text"
    entry = pe["entry_va"]
    if text_image is not None:
        if not expected_text_sha or analysis_entry is None:
            raise ValueError("Captured text requires --text-sha256 and --analysis-entry")
        captured = text_image.read_bytes()
        if digest(captured)!=expected_text_sha or len(captured)!=text["virtual_bytes"]:
            raise ValueError("Captured text identity/length mismatch")
        text["data"] = captured
        code_source = "captured-loaded-text"
        entry = analysis_entry
    elif expected_text_sha is not None or analysis_entry is not None:
        raise ValueError("Analysis entry override is only available with captured text")
    elif pe["sha256"]==STEAM_SHA or pe["entry_section"]==".bind":
        raise ValueError("Packed startup is not an AOT engine entry; supply a verified code image for analysis")
    if not pe["image_base"]+text["rva"]<=entry<pe["image_base"]+text["rva"]+text["virtual_bytes"]:
        raise ValueError("Analysis entry must be within .text")
    # Allocate header tables separately. FNV's eight sections would overlap the
    # fixed name/table layout in a converter written for a smaller executable.
    cert_at, table_at = 0x180, 0x360
    names_at = align(table_at+len(sections)*56,16)
    names = bytearray()
    name_offsets = []
    for s in sections:
        name_offsets.append(names_at+len(names))
        names += s["name"].encode("ascii")+b"\0"
    zeros_at = align(names_at+len(names),4)
    debug_at = zeros_at+8
    if debug_at+32>4096:
        raise ValueError("Analysis header table capacity")
    header = bytearray(4096)
    base = pe["image_base"]
    header[:4] = b"XBEH"
    timestamp = struct.unpack_from("<I", raw, nt+8)[0]
    def put(at, values): struct.pack_into("<"+"I"*len(values), header, at, *values)
    put(0x104, [base,4096,pe["image_bytes"],0x178,timestamp,base+cert_at,
                len(sections),base+table_at,0,entry^0xa8fc57ab])
    stack_commit, heap_reserve, heap_commit = struct.unpack_from("<III",raw,optional+76)
    put(0x12c, [0,stack_commit,heap_reserve,heap_commit,base,pe["image_bytes"],0,
                timestamp,base+debug_at,base+debug_at,0,(base+zeros_at)^0x5b6d40b6])
    put(cert_at, [0x1d0,timestamp,0])
    title = "FNV PC analysis".encode("utf-16-le")
    header[cert_at+12:cert_at+12+len(title)] = title
    header[names_at:names_at+len(names)] = names
    debug_name = b"FalloutNV.exe\0"
    header[debug_at:debug_at+len(debug_name)] = debug_name
    payload = bytearray()
    for i, s in enumerate(sections):
        offset = 4096+align(len(payload),512)
        payload += bytes(offset-4096-len(payload))
        flags = 2 | (4 if s["flags"]&0x20000000 else 0) | (1 if s["flags"]&0x80000000 else 0)
        size = max(s["virtual_bytes"],len(s["data"]))
        put(table_at+i*56, [flags,base+s["rva"],size,offset,len(s["data"]),
                           base+name_offsets[i],0,base+zeros_at,base+zeros_at])
        payload += s["data"]
    image = bytes(header)+payload
    profile = dict(executable_sha256=pe["sha256"],code_source=code_source,
        text_sha256=digest(text["data"]),analysis_container_sha256=digest(image),
        analysis_entry=entry,original_file_entry=pe["entry_va"],image_base=base,
        image_bytes=pe["image_bytes"],normal_import_slots=sum(len(i["symbols"]) for i in pe["imports"]),
        tls_directory_rva=struct.unpack_from("<I",raw,optional+168)[0],
        delay_import_directory_rva=struct.unpack_from("<I",raw,optional+200)[0],
        original_startup_entry_verified=False,native_engine_started=False,
        cold_boot_supported=False,analysis_only=True,
        sections=[dict({k:v for k,v in s.items() if k!="data"},analysis_raw_bytes=len(s["data"])) for s in sections])
    output.mkdir(parents=True,exist_ok=True)
    (output/"fnv-analysis.xbe").write_bytes(image)
    (output/"input-profile.json").write_text(json.dumps(profile,indent=2)+"\n")
    (output/"imports.json").write_text(json.dumps(pe["imports"],indent=2)+"\n")
    return profile

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe",type=Path,required=True)
    p.add_argument("--exe-sha256",required=True)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--text-image",type=Path)
    p.add_argument("--text-sha256")
    p.add_argument("--analysis-entry",type=lambda v:int(v,0))
    a=p.parse_args()
    report=prepare(a.exe,a.output,a.exe_sha256,a.text_image,a.text_sha256,a.analysis_entry)
    print("Prepared private x86 analysis image:",report["code_source"],
          len(report["sections"]),"sections; cold_boot_supported=False")

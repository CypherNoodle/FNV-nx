"""Inventory PE32 imports for native bridge work; no guest or DLL execution.
This is a requirements list, not evidence of implemented or compatible APIs.
"""
import argparse
import collections
import json
from pathlib import Path
from audit_install import pe_info, STEAM_SHA

GROUPS = {
    "d3d9.dll": "graphics", "d3dx9_38.dll": "graphics",
    "xinput1_3.dll": "input", "dinput8.dll": "input",
    "dsound.dll": "audio", "winmm.dll": "audio/timers",
    "binkw32.dll": "video", "libvorbisfile.dll": "audio",
    "steam_api.dll": "store-services", "galaxywrp.dll": "store-services",
}

def audit(executable):
    pe = pe_info(executable)
    libraries = []
    totals = collections.Counter()
    for item in pe["imports"]:
        dll = item["dll"].lower()
        group = GROUPS.get(dll, "windows-system")
        totals[group] += len(item["symbols"])
        libraries.append(dict(dll=dll, group=group, symbols=item["symbols"],
                              required_slots=len(item["symbols"]), native_abi_verified=False))
    return dict(executable_sha256=pe["sha256"], image_base=pe["image_base"],
        image_bytes=pe["image_bytes"], entry_va=pe["entry_va"], entry_section=pe["entry_section"],
        known_steamstub21=pe["sha256"] == STEAM_SHA,
        compiled_aot_profile_available=False, native_engine_started=False,
        normal_import_slots=sum(totals.values()), groups=dict(totals), libraries=libraries,
        limits=["Normal imports only; dynamic imports and vtables need separate auditing",
                "No executable identity has a supported full-engine AOT profile yet",
                "Wine DLL exports and synthetic IAT tokens do not implement native bridges",
                "Presence or absence of imports does not establish DRM-free status"])

if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    args = p.parse_args()
    report = audit(args.exe)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+"\n")
    print("Native requirements:", report["normal_import_slots"], "import slots,",
          len(report["libraries"]), "DLLs; no full-engine AOT profile / no game execution")
    print(json.dumps(report["groups"], sort_keys=True))

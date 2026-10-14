"""Cross-check the native PE reader against the host audit on fixtures/files.
No guest code is executed, including when --game names a real installation.
"""
import argparse, subprocess, sys, tempfile
from pathlib import Path
from pe_fixture import write_pe
pc=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(pc / "tools"))
from audit_install import pe_info

def test(cc, game):
    with tempfile.TemporaryDirectory(prefix="fnv-pe-test-") as tmp:
        root=Path(tmp); exe=root / "reader"
        subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-I"+str(pc / "source"),
            str(pc / "source/fnv_pc_pe.c"),str(pc / "tests/pe_host.c"),"-o",str(exe)],check=True)
        def check(path):
            info=pe_info(path); imports=[i["dll"].lower() for i in info["imports"]]
            r=subprocess.run([str(exe),str(path)],capture_output=True,text=True,check=True)
            lines=r.stdout.splitlines(); head=[int(x) for x in lines[0].split()]
            assert head==[info["image_base"],info["image_bytes"],info["entry_va"]-info["image_base"],
                int(info["entry_section"]==".bind"),int("steam_api.dll" in imports),
                int(bool(set(imports)&{"galaxywrp.dll","galaxy.dll"})),len(imports),int(info['large_address_aware'])]
            assert lines[1:]==imports
        path=root / "sample.exe"
        for dlls,section in (((),".text"),(("KERNEL32.dll","steam_api.dll"),".bind"),(("GalaxyWrp.dll","binkw32.dll"),".text")):
            write_pe(path,dlls,section); check(path)
        good=path.read_bytes()
        for at in (0,64,0x200,1000):
            path.write_bytes(good[:at]); assert subprocess.run([str(exe),str(path)],capture_output=True).returncode==2
        for dll in ("../evil.dll","file/name.dll","foo..dll"):
            write_pe(path,(dll,)); assert subprocess.run([str(exe),str(path)],capture_output=True).returncode==2
        if game:
            check(game / "FalloutNV.exe")
            for name in ("steam_api.dll","binkw32.dll","libvorbisfile.dll","libvorbis.dll"):
                if (game / name).is_file(): check(game / name)
        print("PASS: native/host PE agreement, edition indicators, truncated and unsafe imports; guest code not executed.")

if __name__=="__main__":
    p=argparse.ArgumentParser(); p.add_argument("--cc",default="cc"); p.add_argument("--game",type=Path)
    a=p.parse_args(); test(a.cc,a.game)

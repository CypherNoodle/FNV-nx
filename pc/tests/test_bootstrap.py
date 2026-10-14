"""Exercise the actual C entry adapter with isolated, synthetic file fixtures."""
import argparse, os, subprocess, tempfile
from pathlib import Path

def test(cc, pc):
    with tempfile.TemporaryDirectory(prefix="fnv-pc-test-") as tmp:
        root = Path(tmp); game = root / "pc"; game.mkdir(); (root / "logs").mkdir()
        wine=root / "wine"; documents=wine / "drive_c/users/steamuser/Documents"; documents.mkdir(parents=True)
        exe = root / "bootstrap"
        subprocess.run([cc,"-std=c11","-Wall","-Wextra","-Werror","-Drename=fnv_test_rename",'-DFNV_PC_ROOT="'+root.as_posix()+'"','-DFNV_PC_WINE_ROOT="'+wine.as_posix()+'"',
            "-I"+str(pc / "source"),str(pc / "source/fnv_pc_bootstrap.c"),str(pc / "tests/bootstrap_host.c"),str(pc / "tests/horizon_rename_shim.c"),"-o",str(exe)],check=True)
        names = ["FalloutNV.exe","steam_api.dll","binkw32.dll","libvorbis.dll","libvorbisfile.dll","Fallout_default.ini","Data/FalloutNV.esm",
            "Data/Fallout - Meshes.bsa","Data/Fallout - Misc.bsa","Data/Fallout - Sound.bsa","Data/Fallout - Textures.bsa","Data/Fallout - Textures2.bsa","Data/Fallout - Voices1.bsa"]
        assert subprocess.run([str(exe)],capture_output=True).returncode == 2
        for name in names:
            path = game / name; path.parent.mkdir(exist_ok=True); path.write_text("fixture: no executable game")
        (game / "Fallout_default.ini").write_text("[Display]\nbFull Screen=1\niMultiSample=4\n[General]\ncustomSetting=123\n[Controls]\nbDisable360Controller=1\n")
        ini = root / "fnv-pc.ini"; ini.write_bytes((pc / "fnv-pc.ini").read_bytes())
        good = subprocess.run([str(exe)],capture_output=True,text=True)
        assert good.returncode == 0 and str(game / "FalloutNV.exe") in good.stdout
        assert "buttons=2000,1000,8000,4000,000f" in good.stdout
        prefs=documents / "My Games/FalloutNV/FalloutPrefs.ini"
        assert "iSize W=960\n" in prefs.read_text() and "customSetting=123\n" in prefs.read_text()
        profile = (game / "FalloutNV.wine-nx.txt").read_text()
        assert "cpu=fex\n" in profile and "d3d=dxvk\n" in profile and "dxvk-hud=fps\n" in profile and "four-cores=0\n" in profile
        ini.write_text("[runtime]\ncpu=box64\nrenderer=wine\nbutton_layout=positions\n[graphics]\nfps=0\nvsync=1\nframe_limit=60\npresentation=compositor\n")
        changed = subprocess.run([str(exe)],capture_output=True,text=True)
        assert changed.returncode == 0 and "buttons=1000,2000,4000,8000,000f" in changed.stdout
        assert "iPresentInterval=1\n" in prefs.read_text() and "bDisable360Controller=0\n" in prefs.read_text()
        original=prefs.read_bytes()
        backup=Path(str(prefs)+".fnv-previous"); prefs.rename(backup)
        assert subprocess.run([str(exe)],capture_output=True).returncode == 0
        assert prefs.read_bytes() == original and not backup.exists()
        failed=subprocess.run([str(exe)],capture_output=True,env={**os.environ,"FNV_TEST_FAIL_COMMIT":"1"})
        assert failed.returncode == 2 and prefs.read_bytes() == original
        profile = (game / "FalloutNV.wine-nx.txt").read_text()
        assert "cpu=box64\n" in profile and "d3d=wine\n" in profile and "windows=compositor\n" in profile and "vsync=1\n" in profile
        for bad in ("[runtime]\ncpu=bad\n", "[graphics]\nvsync=2\n", "[runtime]\ncpu=fex\ncpu=box64\n", "[graphics]\nframe_limit=33\n", "[unknown]\nfps=1\n", "[graphics]\nx=1\n", "[graphics]\nwidth=1280\nheight=360\n", "[runtime]\ncpu="+"x"*300+"\n"):
            ini.write_text(bad)
            assert subprocess.run([str(exe)],capture_output=True).returncode == 2
            assert "ini=invalid" in (root / "logs/fnv-pc-bootstrap.log").read_text()
            assert (game / "FalloutNV.wine-nx.txt").read_text() == profile
        print("PASS: required files, FEX/DXVK and Box64/Wine profiles, launch target, malformed/duplicate/unsupported INI values. Engine not executed.")

if __name__ == "__main__":
    p=argparse.ArgumentParser(); p.add_argument("--cc",default="cc")
    test(p.parse_args().cc,Path(__file__).resolve().parents[1])

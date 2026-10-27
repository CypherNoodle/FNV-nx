"""Compile and execute the own layout validator; no game files or audio."""
import argparse
from pathlib import Path
import subprocess
import tempfile

def run(cc):
    pc=Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='engine-layout-',dir=Path.cwd()) as tmp:
        exe=Path(tmp)/'layout.exe'
        subprocess.run([str(cc),'-std=c11','-Wall','-Wextra','-Werror',
                        '-I'+str(pc/'switch'),str(pc/'switch/engine_layout.c'),
                        str(pc/'tests/engine_layout_host.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',type=Path,required=True)
    run(p.parse_args().cc)
